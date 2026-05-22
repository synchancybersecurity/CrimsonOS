/*
 * Crimson OS — Realtek RTL8723CS WiFi Driver
 * Board: PinePhone Pro (Allwinner A64)
 *
 * RTL8723CS is an SDIO 802.11bgn + Bluetooth combo chip.
 * On PinePhone Pro it is connected to the A64's MMC1 (SDIO) controller.
 *
 * A64 MMC1 SDIO base:  0x01C10000
 * Firmware blob path:  /lib/firmware/rtl8723cs_fw.bin
 *
 * Implementation covers:
 *   - A64 MMC1 SDIO host init (CMD52/CMD53 polling)
 *   - RTL8723CS power-on and firmware download
 *   - MAC/PHY init sequence
 *   - Active scan (probe req / beacon collect)
 *   - WPA2-PSK connect:
 *       PBKDF2-SHA1 PMK derivation
 *       PRF-SHA1 PTK derivation
 *       EAPOL 4-way handshake
 *       AES-CCMP temporal key install
 *   - Frame TX/RX wired to net stack via net_rx_frame()
 *
 * Reference: Linux drivers/staging/rtl8723bs/  (GPLv2)
 *            Allwinner A64 User Manual §7.5 SD/MMC Host Controller
 */

#include <crimson/types.h>
#include <crimson/wifi.h>
#include <crimson/printk.h>
#include <crimson/memory.h>
#include <crimson/spinlock.h>
#include <crimson/string.h>
#include <crimson/timer.h>
#include <crimson/net.h>
#include <crimson/fs.h>
#include <crimson/crypto.h>

/* wifi_interface_t, wifi_driver_t, wifi_rx_frame declared via wifi.h */

/* ── A64 MMC1 SDIO host ────────────────────────────────────────── */

#define MMC1_BASE           0x01C10000UL
#define MMC_CTRL            (MMC1_BASE + 0x000)
#define MMC_PWREN           (MMC1_BASE + 0x004)
#define MMC_CLKDIV          (MMC1_BASE + 0x008)
#define MMC_CLKSRC          (MMC1_BASE + 0x00C)
#define MMC_CLKENA          (MMC1_BASE + 0x010)
#define MMC_TMOUT           (MMC1_BASE + 0x014)
#define MMC_CTYPE           (MMC1_BASE + 0x018)
#define MMC_BLKSIZ          (MMC1_BASE + 0x01C)
#define MMC_BYTCNT          (MMC1_BASE + 0x020)
#define MMC_INTMASK         (MMC1_BASE + 0x024)
#define MMC_CMDARG          (MMC1_BASE + 0x028)
#define MMC_CMD             (MMC1_BASE + 0x02C)
#define MMC_RESP0           (MMC1_BASE + 0x030)
#define MMC_RESP1           (MMC1_BASE + 0x034)
#define MMC_RESP2           (MMC1_BASE + 0x038)
#define MMC_RESP3           (MMC1_BASE + 0x03C)
#define MMC_MINTSTS         (MMC1_BASE + 0x040)
#define MMC_RINTSTS         (MMC1_BASE + 0x044)
#define MMC_STATUS          (MMC1_BASE + 0x048)
#define MMC_FIFOTH          (MMC1_BASE + 0x04C)
#define MMC_DATA            (MMC1_BASE + 0x200)

/* MMC_CMD bits */
#define CMD_START           (1U << 31)
#define CMD_USE_HOLD_REG    (1U << 29)
#define CMD_VOLT_SWITCH     (1U << 28)
#define CMD_BOOT_MODE       (1U << 27)
#define CMD_DISABLE_BOOT    (1U << 26)
#define CMD_EXPECT_BOOT_ACK (1U << 25)
#define CMD_ENABLE_BOOT     (1U << 24)
#define CMD_CCS_EXPECTED    (1U << 23)
#define CMD_READ_CEATA      (1U << 22)
#define CMD_UPDATE_CLKS     (1U << 21)
#define CMD_CARD_NUM(n)     ((n) << 16)
#define CMD_SEND_INIT       (1U << 15)
#define CMD_SEND_STOP       (1U << 12)
#define CMD_WAIT_PREV       (1U << 13)
#define CMD_STOP_ABORT      (1U << 14)
#define CMD_WRITE           (1U << 10)
#define CMD_DATA_EXP        (1U << 9)
#define CMD_CHECK_RESP_CRC  (1U << 8)
#define CMD_LONG_RESP       (1U << 7)
#define CMD_RESP_EXP        (1U << 6)
#define CMD_INDEX(n)        ((n) & 0x3F)

/* RINTSTS bits */
#define INT_CMD_DONE        (1U << 2)
#define INT_DATA_OVER       (1U << 3)
#define INT_TXDR            (1U << 4)
#define INT_RXDR            (1U << 5)
#define INT_ERR             (1U << 15)

static inline uint32_t mmc_rd(uintptr_t r) { return *(volatile uint32_t*)r; }
static inline void     mmc_wr(uintptr_t r, uint32_t v) { *(volatile uint32_t*)r = v; }

static int mmc_wait_cmd(uint32_t timeout_us)
{
    while (timeout_us--) {
        uint32_t st = mmc_rd(MMC_RINTSTS);
        if (st & INT_ERR) return -1;
        if (st & INT_CMD_DONE) { mmc_wr(MMC_RINTSTS, INT_CMD_DONE); return 0; }
        for (volatile int i = 0; i < 72; i++);
    }
    return -2;
}

static int mmc_send_cmd(uint32_t cmd_idx, uint32_t arg, uint32_t flags, uint32_t* resp)
{
    mmc_wr(MMC_RINTSTS, 0xFFFFFFFF);
    mmc_wr(MMC_CMDARG,  arg);
    mmc_wr(MMC_CMD,     CMD_START | CMD_USE_HOLD_REG | CMD_INDEX(cmd_idx) | flags);

    if (mmc_wait_cmd(100000) < 0) return -1;
    if (resp) *resp = mmc_rd(MMC_RESP0);
    return 0;
}

/* ── SDIO CMD52 (single-byte R/W) and CMD53 (multi-byte) ──────── */

#define SDIO_FUNC_WIFI  1
#define CMD52_WRITE     (1U << 31)
#define CMD52_RAW       (1U << 27)
#define CMD53_WRITE     (1U << 31)
#define CMD53_BLOCK     (1U << 27)
#define CMD53_INCR      (1U << 26)

static int sdio_cmd52(uint32_t func, uint32_t addr, uint8_t data,
                       int write, uint8_t* out)
{
    uint32_t arg = ((func & 7) << 28) | ((addr & 0x1FFFF) << 9) | (data & 0xFF);
    if (write) arg |= CMD52_WRITE;
    uint32_t resp;
    if (mmc_send_cmd(52, arg, CMD_RESP_EXP | CMD_CHECK_RESP_CRC, &resp) < 0)
        return -1;
    if (out) *out = (uint8_t)(resp & 0xFF);
    return 0;
}

/* Read up to 512 bytes from SDIO function via CMD53 */
static int sdio_cmd53_read(uint32_t func, uint32_t addr, uint8_t* buf, uint32_t len)
{
    if (len > 512 || len == 0) return -1;

    mmc_wr(MMC_BLKSIZ, len);
    mmc_wr(MMC_BYTCNT, len);
    mmc_wr(MMC_INTMASK, 0);
    mmc_wr(MMC_RINTSTS, 0xFFFFFFFF);

    uint32_t arg = ((func & 7) << 28) | CMD53_INCR | ((addr & 0x1FFFF) << 9) | (len & 0x1FF);
    uint32_t cmd_flags = CMD_RESP_EXP | CMD_CHECK_RESP_CRC | CMD_DATA_EXP;
    mmc_wr(MMC_CMDARG, arg);
    mmc_wr(MMC_CMD, CMD_START | CMD_USE_HOLD_REG | CMD_INDEX(53) | cmd_flags);

    if (mmc_wait_cmd(100000) < 0) return -1;

    /* Drain FIFO */
    for (uint32_t i = 0; i < (len + 3) / 4; i++) {
        uint32_t d = mmc_rd(MMC_DATA);
        uint32_t off = i * 4;
        if (off     < len) buf[off]     = (uint8_t)(d);
        if (off + 1 < len) buf[off + 1] = (uint8_t)(d >> 8);
        if (off + 2 < len) buf[off + 2] = (uint8_t)(d >> 16);
        if (off + 3 < len) buf[off + 3] = (uint8_t)(d >> 24);
    }
    return 0;
}

static int sdio_cmd53_write(uint32_t func, uint32_t addr,
                             const uint8_t* buf, uint32_t len)
{
    if (len > 512 || len == 0) return -1;

    mmc_wr(MMC_BLKSIZ, len);
    mmc_wr(MMC_BYTCNT, len);
    mmc_wr(MMC_RINTSTS, 0xFFFFFFFF);

    /* Pre-fill FIFO */
    for (uint32_t i = 0; i < (len + 3) / 4; i++) {
        uint32_t d = 0;
        uint32_t off = i * 4;
        if (off     < len) d |= (uint32_t)buf[off];
        if (off + 1 < len) d |= (uint32_t)buf[off + 1] << 8;
        if (off + 2 < len) d |= (uint32_t)buf[off + 2] << 16;
        if (off + 3 < len) d |= (uint32_t)buf[off + 3] << 24;
        mmc_wr(MMC_DATA, d);
    }

    uint32_t arg = ((func & 7) << 28) | CMD53_WRITE | CMD53_INCR |
                   ((addr & 0x1FFFF) << 9) | (len & 0x1FF);
    uint32_t cmd_flags = CMD_RESP_EXP | CMD_CHECK_RESP_CRC | CMD_DATA_EXP | CMD_WRITE;
    mmc_wr(MMC_CMDARG, arg);
    mmc_wr(MMC_CMD, CMD_START | CMD_USE_HOLD_REG | CMD_INDEX(53) | cmd_flags);
    return mmc_wait_cmd(100000);
}

/* Convenience: write/read RTL8723CS register (1/2/4 bytes) */
static void rtl_write8(uint32_t addr, uint8_t val)
{
    sdio_cmd52(SDIO_FUNC_WIFI, addr, val, 1, NULL);
}
static uint8_t rtl_read8(uint32_t addr)
{
    uint8_t v = 0;
    sdio_cmd52(SDIO_FUNC_WIFI, addr, 0, 0, &v);
    return v;
}
static void rtl_write32(uint32_t addr, uint32_t val)
{
    uint8_t b[4] = { val & 0xFF, (val>>8)&0xFF, (val>>16)&0xFF, (val>>24)&0xFF };
    sdio_cmd53_write(SDIO_FUNC_WIFI, addr, b, 4);
}
static uint32_t rtl_read32(uint32_t addr)
{
    uint8_t b[4] = {0};
    sdio_cmd53_read(SDIO_FUNC_WIFI, addr, b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}

/* ── RTL8723CS register map (subset) ─────────────────────────── */

#define REG_SYS_FUNC_EN     0x0002
#define REG_APS_FSMCO       0x0004
#define REG_SYS_CLKR        0x0008
#define REG_SYS_ISO_CTRL    0x000E
#define REG_SYS_SWR_CTRL1   0x0010
#define REG_AFE_CTRL1       0x0024
#define REG_RSV_CTRL        0x001C
#define REG_RF_CTRL         0x001F
#define REG_LDOV12D_CTRL    0x0021
#define REG_SYS_CFG         0x00F0
#define REG_CR              0x0100
#define REG_TRXDMA_CTRL     0x010C
#define REG_TXDMA_OFFSET_CHK 0x020C
#define REG_TRXFF_BNDY      0x0114
#define REG_RXFF_BNDY       0x0118
#define REG_PBP             0x0104
#define REG_HIMR            0x0014      /* SDIO host interrupt mask */
#define REG_HISR            0x0018      /* SDIO host interrupt status */
#define REG_TX_RPT_CTRL     0x049C
#define REG_MACID           0x0050
#define REG_BSSID           0x0618
#define REG_RCR             0x0608      /* Receive configuration */
#define REG_TCR             0x0604      /* Transmit configuration */
#define REG_BCN_CTRL        0x0550
#define REG_FWHW_TXQ_CTRL   0x0420
#define REG_RQPN            0x0200
#define REG_FIFOPAGE        0x0204
#define REG_DWBCN0_CTRL     0x0228

/* APS_FSMCO bits */
#define APS_FSMCO_MAC_ENABLE    (1U << 8)
#define APS_FSMCO_APFM_ONMAC    (1U << 0)
#define APS_FSMCO_AFSM_PCIE     (1U << 1)

/* CR bits */
#define CR_HCI_TXDMA_EN         (1U << 5)
#define CR_HCI_RXDMA_EN         (1U << 4)
#define CR_TXDMA_EN             (1U << 3)
#define CR_RXDMA_EN             (1U << 2)
#define CR_PROTOCOL_EN          (1U << 1)
#define CR_MACTXEN              (1U << 1)
#define CR_MACRXEN              (1U << 0)

/* ── SHA-1 (needed for PBKDF2 / HMAC in WPA2-PSK) ─────────────── */

typedef struct { uint32_t h[5]; uint8_t buf[64]; uint32_t len_lo, len_hi; uint32_t buflen; } sha1_ctx_t;

#define SHA1_ROTL(v,n) (((v)<<(n))|((v)>>(32-(n))))

static void sha1_init(sha1_ctx_t* c)
{
    c->h[0]=0x67452301; c->h[1]=0xEFCDAB89; c->h[2]=0x98BADCFE;
    c->h[3]=0x10325476; c->h[4]=0xC3D2E1F0;
    c->len_lo = c->len_hi = c->buflen = 0;
}

static void sha1_process_block(sha1_ctx_t* c, const uint8_t blk[64])
{
    uint32_t w[80], a,b,d,e,f,k,t;
    uint32_t cc = c->h[2]; /* avoid collision with c */
    for (int i=0;i<16;i++)
        w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|
             ((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
    for (int i=16;i<80;i++)
        w[i]=SHA1_ROTL(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    a=c->h[0]; b=c->h[1]; cc=c->h[2]; d=c->h[3]; e=c->h[4];
    for (int i=0;i<80;i++){
        if     (i<20){f=(b&cc)|(~b&d);k=0x5A827999;}
        else if(i<40){f=b^cc^d;      k=0x6ED9EBA1;}
        else if(i<60){f=(b&cc)|(b&d)|(cc&d);k=0x8F1BBCDC;}
        else         {f=b^cc^d;      k=0xCA62C1D6;}
        t=SHA1_ROTL(a,5)+f+e+k+w[i]; e=d; d=cc; cc=SHA1_ROTL(b,30); b=a; a=t;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e;
}

static void sha1_update(sha1_ctx_t* c, const uint8_t* data, uint32_t len)
{
    while (len--) {
        c->buf[c->buflen++] = *data++;
        c->len_lo += 8;
        if (c->len_lo == 0) c->len_hi++;
        if (c->buflen == 64) { sha1_process_block(c, c->buf); c->buflen = 0; }
    }
}

static void sha1_final(sha1_ctx_t* c, uint8_t digest[20])
{
    uint8_t pad[64]; uint32_t padbytes;
    pad[0] = 0x80;
    padbytes = (c->buflen < 56) ? (55 - c->buflen) : (119 - c->buflen);
    memset(pad+1, 0, padbytes);
    uint32_t total = c->buflen + 1 + padbytes;
    /* append big-endian bit length */
    pad[total]   = (uint8_t)(c->len_hi >> 24);
    pad[total+1] = (uint8_t)(c->len_hi >> 16);
    pad[total+2] = (uint8_t)(c->len_hi >> 8);
    pad[total+3] = (uint8_t)(c->len_hi);
    pad[total+4] = (uint8_t)(c->len_lo >> 24);
    pad[total+5] = (uint8_t)(c->len_lo >> 16);
    pad[total+6] = (uint8_t)(c->len_lo >> 8);
    pad[total+7] = (uint8_t)(c->len_lo);
    sha1_update(c, pad, total + 8);
    for (int i=0;i<5;i++){
        digest[i*4]  =(uint8_t)(c->h[i]>>24); digest[i*4+1]=(uint8_t)(c->h[i]>>16);
        digest[i*4+2]=(uint8_t)(c->h[i]>>8);  digest[i*4+3]=(uint8_t)(c->h[i]);
    }
}

static void hmac_sha1(const uint8_t* key, uint32_t klen,
                       const uint8_t* msg, uint32_t mlen,
                       uint8_t out[20])
{
    uint8_t ipad[64], opad[64], k0[20];
    sha1_ctx_t ctx;
    if (klen > 64) { sha1_init(&ctx); sha1_update(&ctx,key,klen); sha1_final(&ctx,k0); key=k0; klen=20; }
    memset(ipad, 0x36, 64); memset(opad, 0x5C, 64);
    for (uint32_t i=0; i<klen; i++) { ipad[i]^=key[i]; opad[i]^=key[i]; }
    sha1_init(&ctx); sha1_update(&ctx,ipad,64); sha1_update(&ctx,msg,mlen); sha1_final(&ctx,out);
    sha1_init(&ctx); sha1_update(&ctx,opad,64); sha1_update(&ctx,out,20);  sha1_final(&ctx,out);
}

/* PBKDF2-SHA1: derive `dklen` bytes into `dk` from password+salt */
static void pbkdf2_sha1(const uint8_t* pwd, uint32_t plen,
                          const uint8_t* salt, uint32_t slen,
                          uint32_t iterations, uint8_t* dk, uint32_t dklen)
{
    uint8_t u[20], t[20];
    uint32_t block_num = 0;
    for (uint32_t done = 0; done < dklen; ) {
        block_num++;
        /* U1 = HMAC-SHA1(pwd, salt || INT(block_num)) */
        uint8_t* sbuf = kmalloc(slen + 4);
        if (!sbuf) return;
        memcpy(sbuf, salt, slen);
        sbuf[slen]   = (uint8_t)(block_num >> 24);
        sbuf[slen+1] = (uint8_t)(block_num >> 16);
        sbuf[slen+2] = (uint8_t)(block_num >> 8);
        sbuf[slen+3] = (uint8_t)(block_num);
        hmac_sha1(pwd, plen, sbuf, slen+4, u);
        kfree(sbuf);
        memcpy(t, u, 20);
        for (uint32_t c=1; c<iterations; c++) {
            hmac_sha1(pwd, plen, u, 20, u);
            for (int j=0; j<20; j++) t[j] ^= u[j];
        }
        uint32_t take = dklen - done;
        if (take > 20) take = 20;
        memcpy(dk + done, t, take);
        done += take;
    }
}

/* PRF-SHA1: WPA2 pseudo-random function (802.11i §H.5) */
static void prf_sha1(const uint8_t* key, uint32_t klen,
                      const char* label,
                      const uint8_t* data, uint32_t dlen,
                      uint8_t* out, uint32_t outlen)
{
    uint32_t llen = strlen(label);
    uint8_t* buf = kmalloc(llen + 1 + dlen + 1);
    if (!buf) return;
    memcpy(buf, label, llen);
    buf[llen] = 0x00;
    memcpy(buf + llen + 1, data, dlen);
    for (uint32_t done = 0, i = 0; done < outlen; done += 20, i++) {
        buf[llen + 1 + dlen] = (uint8_t)i;
        uint8_t tmp[20];
        hmac_sha1(key, klen, buf, llen + 2 + dlen, tmp);
        uint32_t take = outlen - done; if (take > 20) take = 20;
        memcpy(out + done, tmp, take);
    }
    kfree(buf);
}

/* ── WPA2-PSK state and handshake ─────────────────────────────── */

typedef struct {
    uint8_t  pmk[32];           /* Pairwise Master Key */
    uint8_t  ptk[64];           /* Pairwise Transient Key (512 bits) */
    uint8_t  kck[16];           /* Key Confirmation Key (PTK[0:16]) */
    uint8_t  kek[16];           /* Key Encryption Key  (PTK[16:32]) */
    uint8_t  tk[16];            /* Temporal Key        (PTK[32:48]) */
    uint8_t  anonce[32];
    uint8_t  snonce[32];
    uint8_t  aa[6];             /* Authenticator (AP) MAC */
    uint8_t  spa[6];            /* Supplicant (STA) MAC */
    uint32_t replay_counter_hi;
    uint32_t replay_counter_lo;
    int      handshake_step;    /* 0=idle, 1=got M1, 2=sent M2, 3=got M3 */
} wpa_state_t;

static wpa_state_t g_wpa;

static void wpa_derive_ptk(void)
{
    /* B = min(AA,SPA)||max(AA,SPA)||min(ANonce,SNonce)||max(ANonce,SNonce) */
    uint8_t b[76];
    int aa_lt = (memcmp(g_wpa.aa, g_wpa.spa, 6) <= 0);
    memcpy(b +  0, aa_lt ? g_wpa.aa : g_wpa.spa, 6);
    memcpy(b +  6, aa_lt ? g_wpa.spa : g_wpa.aa, 6);
    int an_lt = (memcmp(g_wpa.anonce, g_wpa.snonce, 32) <= 0);
    memcpy(b + 12, an_lt ? g_wpa.anonce : g_wpa.snonce, 32);
    memcpy(b + 44, an_lt ? g_wpa.snonce : g_wpa.anonce, 32);

    prf_sha1(g_wpa.pmk, 32, "Pairwise key expansion", b, 76, g_wpa.ptk, 64);
    memcpy(g_wpa.kck, g_wpa.ptk,      16);
    memcpy(g_wpa.kek, g_wpa.ptk + 16, 16);
    memcpy(g_wpa.tk,  g_wpa.ptk + 32, 16);
}

/* Compute MIC over EAPOL frame using HMAC-SHA1 truncated to 16 bytes */
static void wpa_compute_mic(const uint8_t* frame, uint32_t len,
                              uint8_t mic_out[16])
{
    uint8_t full[20];
    hmac_sha1(g_wpa.kck, 16, frame, len, full);
    memcpy(mic_out, full, 16);
}

/* ── Firmware download ─────────────────────────────────────────── */

static int rtl8723cs_load_firmware(void)
{
    void*   fw_data = NULL;
    size_t  fw_size = 0;

    int fd = fs_open("/lib/firmware/rtl8723cs_fw.bin", FS_O_RDONLY);
    if (fd < 0) {
        printk(KERN_WARN "rtl8723cs: firmware not found at /lib/firmware/rtl8723cs_fw.bin\n");
        return -1;
    }
    fs_stat_t st;
    fs_fstat(fd, &st);
    fw_size = (size_t)st.size;
    if (fw_size < 32 || fw_size > 256 * 1024) { fs_close(fd); return -1; }

    fw_data = kmalloc(fw_size);
    if (!fw_data) { fs_close(fd); return -1; }
    fs_read(fd, fw_data, fw_size);
    fs_close(fd);

    /* Tell chip to enter firmware download mode */
    uint8_t cr = rtl_read8(REG_CR);
    rtl_write8(REG_CR, cr | 0x01);       /* enable CPU */
    timer_delay_ms(2);

    /* Download in 256-byte chunks to the firmware buffer register (0x1000) */
    const uint8_t* p = (const uint8_t*)fw_data;
    uint32_t off = 0;
    while (off < fw_size) {
        uint32_t chunk = fw_size - off;
        if (chunk > 256) chunk = 256;
        sdio_cmd53_write(SDIO_FUNC_WIFI, 0x1000, p + off, (uint8_t)chunk);
        off += chunk;
        timer_delay_ms(1);
    }

    /* Signal firmware download complete */
    rtl_write8(0x80, 0x01);   /* HIMR: enable firmware ready interrupt */
    timer_delay_ms(10);

    kfree(fw_data);
    printk(KERN_INFO "rtl8723cs: firmware downloaded (%lu bytes)\n", (unsigned long)fw_size);
    return 0;
}

/* ── MAC init ──────────────────────────────────────────────────── */

static int rtl8723cs_mac_init(const uint8_t mac[6])
{
    /* Power on sequence */
    rtl_write8(REG_RSV_CTRL, 0x00);
    rtl_write32(REG_SYS_SWR_CTRL1, 0x00A300B3);
    timer_delay_ms(1);
    rtl_write8(REG_SYS_FUNC_EN + 1, 0x54);
    rtl_write8(REG_SYS_FUNC_EN, 0xE0);
    rtl_write8(REG_RF_CTRL, 0x07);
    rtl_write8(REG_LDOV12D_CTRL, 0x65);
    rtl_write32(REG_AFE_CTRL1, 0x00DB25A0);
    timer_delay_ms(5);

    /* Enable MAC functions */
    uint32_t val = rtl_read32(REG_APS_FSMCO);
    val |= APS_FSMCO_MAC_ENABLE;
    rtl_write32(REG_APS_FSMCO, val);
    timer_delay_ms(10);

    /* Enable TX/RX DMA */
    rtl_write32(REG_CR, CR_HCI_TXDMA_EN | CR_HCI_RXDMA_EN |
                         CR_TXDMA_EN | CR_RXDMA_EN |
                         CR_PROTOCOL_EN | CR_MACTXEN | CR_MACRXEN);

    /* Set MAC address */
    sdio_cmd53_write(SDIO_FUNC_WIFI, REG_MACID, mac, 6);

    /* Configure RX: receive all unicast + broadcast + multicast */
    rtl_write32(REG_RCR, 0x7000208E);

    /* Disable SDIO interrupts (polling mode for now) */
    rtl_write32(REG_HIMR, 0);

    printk(KERN_INFO "rtl8723cs: MAC init done, addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return 0;
}

/* ── SDIO host init ─────────────────────────────────────────────── */

static int sdio_host_init(void)
{
    /* A64 CCU: enable MMC1 clock and de-assert reset */
    #define A64_CCU_BASE        0x01C20000UL
    #define CCU_SD1_CLK         (A64_CCU_BASE + 0x090)   /* SD1 clock config */
    #define CCU_BUS_CLK_GATE0   (A64_CCU_BASE + 0x060)   /* bit 9 = SDMMC1 */
    #define CCU_BUS_SOFT_RST0   (A64_CCU_BASE + 0x2C0)   /* bit 9 = SDMMC1 */

    /* Source from PLL_PERIPH0 / 4 ≈ 100 MHz, then MMC divides to ~25 MHz */
    *(volatile uint32_t*)CCU_SD1_CLK = (1U<<31)|(2U<<24)|3;  /* enable, PLL_PERIPH0, N=0, M=3 */
    *(volatile uint32_t*)CCU_BUS_CLK_GATE0 |= (1U<<9);
    *(volatile uint32_t*)CCU_BUS_SOFT_RST0 |= (1U<<9);
    timer_delay_ms(1);

    /* Controller reset */
    mmc_wr(MMC_CTRL, (1U<<0)|(1U<<1)|(1U<<2));  /* reset controller + FIFOs */
    uint32_t to = 10000;
    while (to-- && (mmc_rd(MMC_CTRL) & 0x7)) ;

    mmc_wr(MMC_PWREN,   1);            /* power on card */
    mmc_wr(MMC_CLKDIV,  0x00070007);   /* div 8 (~3 MHz init freq) */
    mmc_wr(MMC_CLKENA,  1);            /* enable clock */
    mmc_wr(MMC_CLKSRC,  0);            /* source from CCU */
    mmc_wr(MMC_TMOUT,   0xFFFFFFFF);
    mmc_wr(MMC_FIFOTH,  0x000F0007);   /* burst size 8, threshold 16 */
    mmc_wr(MMC_INTMASK, 0);
    mmc_wr(MMC_RINTSTS, 0xFFFFFFFF);

    /* Dummy clock update command */
    mmc_wr(MMC_CMD, CMD_START | CMD_WAIT_PREV | CMD_UPDATE_CLKS);
    timer_delay_ms(2);

    /* CMD0: go idle */
    mmc_send_cmd(0, 0, CMD_SEND_INIT, NULL);
    timer_delay_ms(5);

    /* CMD5: IO_SEND_OP_COND — check it's an SDIO card */
    uint32_t resp;
    mmc_send_cmd(5, 0, CMD_RESP_EXP, &resp);
    timer_delay_ms(2);

    /* CMD3: get RCA */
    uint32_t rca = 0;
    mmc_send_cmd(3, 0, CMD_RESP_EXP | CMD_CHECK_RESP_CRC, &rca);
    rca = (rca >> 16) & 0xFFFF;

    /* CMD7: select card */
    mmc_send_cmd(7, rca << 16, CMD_RESP_EXP | CMD_CHECK_RESP_CRC, NULL);
    timer_delay_ms(2);

    /* Switch to 4-bit bus width via CCCR */
    sdio_cmd52(0, 0x07, 0x02, 1, NULL);   /* CCCR bus interface control */
    mmc_wr(MMC_CTYPE, 1);   /* 4-bit mode */

    /* Raise clock to ~25 MHz: div=2 */
    mmc_wr(MMC_CLKDIV, 0x00010001);
    mmc_wr(MMC_CMD, CMD_START | CMD_WAIT_PREV | CMD_UPDATE_CLKS);
    timer_delay_ms(1);

    /* Enable SDIO function 1 (WiFi) via CCCR I/O Enable */
    sdio_cmd52(0, 0x02, 0x02, 1, NULL);   /* CCCR IO Enable: func 1 */

    /* Wait for function to become ready */
    to = 1000;
    while (to--) {
        uint8_t ready = 0;
        sdio_cmd52(0, 0x03, 0, 0, &ready);  /* CCCR IO Ready */
        if (ready & 0x02) break;
        timer_delay_ms(2);
    }

    printk(KERN_INFO "rtl8723cs: SDIO host initialised, card RCA=0x%04x\n", rca);
    return 0;
}

/* ── Scan ────────────────────────────────────────────────────── */

/* Build and send an 802.11 Probe Request frame */
static int rtl8723cs_send_probe_req(wifi_interface_t* iface, uint32_t freq)
{
    uint8_t frame[64];
    uint32_t len = 0;

    /* 802.11 MAC header */
    frame[len++] = 0x40;  /* Frame control: probe request */
    frame[len++] = 0x00;
    frame[len++] = 0x00; frame[len++] = 0x00;  /* Duration */
    memset(frame + len, 0xFF, 6); len += 6;      /* DA: broadcast */
    memcpy(frame + len, iface->mac, 6); len += 6; /* SA */
    memset(frame + len, 0xFF, 6); len += 6;      /* BSSID: broadcast */
    frame[len++] = 0x00; frame[len++] = 0x00;   /* Sequence */

    /* SSID IE (empty = wildcard) */
    frame[len++] = 0x00; frame[len++] = 0x00;

    /* Supported rates IE */
    frame[len++] = 0x01; frame[len++] = 0x04;
    frame[len++] = 0x82; frame[len++] = 0x84; /* 1, 2 Mbps */
    frame[len++] = 0x8B; frame[len++] = 0x96; /* 5.5, 11 Mbps */

    /* Set channel before sending */
    rtl_write8(0x07C3, (uint8_t)((freq - 2412) / 5 + 1));  /* simplified */

    return sdio_cmd53_write(SDIO_FUNC_WIFI, 0x8000, frame, len);
}

static int rtl8723cs_start_scan(wifi_interface_t* iface)
{
    static const uint32_t chans[] = {
        2412,2417,2422,2427,2432,2437,2442,2447,2452,2457,2462,2467,2472
    };
    for (int i = 0; i < 13; i++) {
        rtl8723cs_send_probe_req(iface, chans[i]);
        timer_delay_ms(20);
    }
    return 0;
}

/* ── Connect / 4-way handshake ───────────────────────────────── */

static int rtl8723cs_connect(wifi_interface_t* iface,
                               const uint8_t* ssid, const uint8_t* key)
{
    /* Derive PMK from passphrase via PBKDF2-SHA1 */
    pbkdf2_sha1(key, strlen((const char*)key),
                ssid, strlen((const char*)ssid),
                4096, g_wpa.pmk, 32);
    memcpy(g_wpa.spa, iface->mac, 6);
    g_wpa.handshake_step = 0;

    /* Send 802.11 Authentication frame (Open System, seq 1) */
    uint8_t auth[32] = {0};
    auth[0] = 0xB0;  /* Auth frame */
    auth[2] = 0x3A;  /* Duration */
    memcpy(auth + 4,  iface->connected_bssid, 6); /* DA */
    memcpy(auth + 10, iface->mac, 6);             /* SA */
    memcpy(auth + 16, iface->connected_bssid, 6); /* BSSID */
    /* seq=1, alg=Open, status=0 */
    sdio_cmd53_write(SDIO_FUNC_WIFI, 0x8000, auth, 30);
    timer_delay_ms(50);

    /* Send Association Request */
    uint8_t assoc[64];
    uint32_t alen = 0;
    assoc[alen++] = 0x00; assoc[alen++] = 0x00; /* Frame ctrl: Assoc Req */
    assoc[alen++] = 0x3A; assoc[alen++] = 0x00; /* Duration */
    memcpy(assoc + alen, iface->connected_bssid, 6); alen += 6;
    memcpy(assoc + alen, iface->mac, 6);              alen += 6;
    memcpy(assoc + alen, iface->connected_bssid, 6); alen += 6;
    assoc[alen++] = 0x00; assoc[alen++] = 0x00;   /* Seq */
    assoc[alen++] = 0x11; assoc[alen++] = 0x04;   /* Capabilities, listen interval */
    /* SSID IE */
    uint8_t ssid_len = (uint8_t)strlen((const char*)ssid);
    assoc[alen++] = 0x00; assoc[alen++] = ssid_len;
    memcpy(assoc + alen, ssid, ssid_len); alen += ssid_len;
    /* Supported rates */
    assoc[alen++] = 0x01; assoc[alen++] = 0x04;
    assoc[alen++] = 0x82; assoc[alen++] = 0x84; assoc[alen++] = 0x8B; assoc[alen++] = 0x96;
    /* RSN IE for WPA2-PSK-CCMP */
    static const uint8_t rsn_ie[] = {
        0x30, 0x14,                         /* RSN IE, length 20 */
        0x01,0x00,                           /* version 1 */
        0x00,0x0F,0xAC,0x04,               /* group cipher: CCMP */
        0x01,0x00,                           /* pairwise count: 1 */
        0x00,0x0F,0xAC,0x04,               /* pairwise: CCMP */
        0x01,0x00,                           /* AKM count: 1 */
        0x00,0x0F,0xAC,0x02,               /* AKM: PSK */
        0x00,0x00                            /* RSN capabilities */
    };
    memcpy(assoc + alen, rsn_ie, sizeof(rsn_ie)); alen += sizeof(rsn_ie);
    sdio_cmd53_write(SDIO_FUNC_WIFI, 0x8000, assoc, alen);

    printk(KERN_INFO "rtl8723cs: association request sent to %02x:%02x:%02x:%02x:%02x:%02x\n",
           iface->connected_bssid[0], iface->connected_bssid[1], iface->connected_bssid[2],
           iface->connected_bssid[3], iface->connected_bssid[4], iface->connected_bssid[5]);
    return 0;
}

/*
 * Called by wifi_rx_frame() when an EAPOL frame arrives on an
 * associating interface.
 *
 * Implements the supplicant side of 802.11i §8.5.6.4:
 *   M1 received → generate SNonce, derive PTK, send M2
 *   M3 received → verify MIC, install keys, send M4
 */
void rtl8723cs_eapol_rx(wifi_interface_t* iface,
                          const uint8_t* frame, uint32_t len)
{
    if (len < 99) return;
    /* EAPOL-Key layout:
     *   ETH header (14) + EAPOL header (4) + Key descriptor (95+)
     *   Key info at offset 14+4+1 = 19 (2 bytes)
     *   ANonce at offset 14+4+17 = 35 (32 bytes)
     *   MIC at offset 14+4+77 = 95 (16 bytes) */
    const uint8_t* eapol = frame + 14;
    if (eapol[0] != 0x02 || eapol[1] != 0x03) return;  /* not EAPOL-Key */

    uint16_t key_info = (uint16_t)((eapol[5] << 8) | eapol[6]);
    int is_pairwise = !!(key_info & (1U << 3));
    int is_install  = !!(key_info & (1U << 6));
    int is_ack      = !!(key_info & (1U << 7));
    int has_mic     = !!(key_info & (1U << 8));

    if (is_pairwise && is_ack && !has_mic && g_wpa.handshake_step == 0) {
        /* Message 1: extract ANonce, generate SNonce, derive PTK, send M2 */
        memcpy(g_wpa.anonce, eapol + 17, 32);
        memcpy(g_wpa.aa, frame + 6, 6);     /* AP MAC from Ethernet src */
        rng_get_bytes(g_wpa.snonce, 32);
        wpa_derive_ptk();

        /* Build EAPOL-Key Message 2 */
        uint8_t m2[121];
        memset(m2, 0, sizeof(m2));
        /* Ethernet header */
        memcpy(m2, g_wpa.aa, 6);            /* DA = AP */
        memcpy(m2 + 6, iface->mac, 6);      /* SA = STA */
        m2[12] = 0x88; m2[13] = 0x8E;       /* EtherType = EAPOL */
        /* EAPOL header */
        m2[14] = 0x02; m2[15] = 0x03;       /* version=2, type=Key */
        m2[16] = 0x00; m2[17] = 0x5F;       /* length = 95 */
        /* Key descriptor */
        m2[18] = 0x02;  /* Key descriptor type: RSN */
        uint16_t ki2 = 0x0109 | (1U<<3);    /* pairwise, MIC */
        m2[19] = (uint8_t)(ki2 >> 8); m2[20] = (uint8_t)ki2;
        m2[21] = 0x00; m2[22] = 0x00;       /* replay counter hi (copy from M1) */
        memcpy(m2 + 27, eapol + 25, 8);     /* replay counter from M1 */
        memcpy(m2 + 35, g_wpa.snonce, 32);  /* SNonce */
        /* MIC field placeholder (bytes 95-110 in EAPOL = m2[109..124]) */
        /* Compute MIC over frame with MIC field zeroed */
        uint8_t mic[16];
        wpa_compute_mic(m2 + 14, 99, mic);   /* over EAPOL portion */
        memcpy(m2 + 95, mic, 16);
        /* RSN IE */
        static const uint8_t rsn_short[] = {
            0x30,0x14,0x01,0x00,0x00,0x0F,0xAC,0x04,
            0x01,0x00,0x00,0x0F,0xAC,0x04,0x01,0x00,
            0x00,0x0F,0xAC,0x02,0x00,0x00
        };
        memcpy(m2 + 111, rsn_short, sizeof(rsn_short));
        sdio_cmd53_write(SDIO_FUNC_WIFI, 0x8000, m2, sizeof(m2));

        g_wpa.handshake_step = 1;
        printk(KERN_DEBUG "wpa2: M2 sent\n");
    }
    else if (is_pairwise && is_install && has_mic && g_wpa.handshake_step == 1) {
        /* Message 3: verify MIC, install keys, send M4 */
        uint8_t rx_mic[16], check_frame[512];
        uint32_t frame_len = len - 14;
        if (frame_len > 512) return;
        memcpy(check_frame, frame + 14, frame_len);
        memcpy(rx_mic, check_frame + 77, 16);
        memset(check_frame + 77, 0, 16);   /* zero MIC field */
        uint8_t calc_mic[16];
        wpa_compute_mic(check_frame, frame_len, calc_mic);
        /* Constant-time compare */
        uint8_t diff = 0;
        for (int i = 0; i < 16; i++) diff |= rx_mic[i] ^ calc_mic[i];
        if (diff) { printk(KERN_WARN "wpa2: M3 MIC failed\n"); return; }

        /* Install TK into hardware */
        sdio_cmd53_write(SDIO_FUNC_WIFI, 0x0E10, g_wpa.tk, 16);  /* TK register */
        rtl_write8(0x0E08, 0x07);   /* enable CCMP */

        /* Send Message 4 */
        uint8_t m4[77];
        memset(m4, 0, sizeof(m4));
        memcpy(m4, g_wpa.aa, 6); memcpy(m4 + 6, iface->mac, 6);
        m4[12] = 0x88; m4[13] = 0x8E;
        m4[14] = 0x02; m4[15] = 0x03; m4[16] = 0x00; m4[17] = 0x5F;
        m4[18] = 0x02;
        uint16_t ki4 = 0x0109 | (1U<<3);
        m4[19] = (uint8_t)(ki4>>8); m4[20] = (uint8_t)ki4;
        memcpy(m4 + 27, eapol + 25, 8);   /* replay counter */
        uint8_t m4_mic[16];
        wpa_compute_mic(m4 + 14, 63, m4_mic);
        memcpy(m4 + 77, m4_mic, 16);       /* append MIC */
        sdio_cmd53_write(SDIO_FUNC_WIFI, 0x8000, m4, sizeof(m4));

        g_wpa.handshake_step = 2;
        iface->state = 5; /* WIFI_CONNECTED */
        printk(KERN_INFO "wpa2: handshake complete — link up\n");
    }
}

static int rtl8723cs_disconnect(wifi_interface_t* iface)
{
    uint8_t disassoc[28] = {0};
    disassoc[0] = 0xA0;  /* Disassociation */
    memcpy(disassoc + 4,  iface->connected_bssid, 6);
    memcpy(disassoc + 10, iface->mac, 6);
    memcpy(disassoc + 16, iface->connected_bssid, 6);
    disassoc[24] = 0x03; disassoc[25] = 0x00;  /* reason: deauthenticated */
    sdio_cmd53_write(SDIO_FUNC_WIFI, 0x8000, disassoc, 26);
    return 0;
}

static int rtl8723cs_set_channel(wifi_interface_t* iface, uint32_t freq, uint32_t bw)
{
    (void)iface; (void)bw;
    uint8_t ch = (uint8_t)((freq >= 5180) ?
        ((freq - 5180) / 20 + 36) :
        ((freq - 2412) / 5 + 1));
    rtl_write8(0x07C3, ch);
    return 0;
}

static int rtl8723cs_set_mode(wifi_interface_t* iface, uint32_t mode)
{
    (void)iface;
    /* 0=STA: RCR normal; 2=Monitor: enable promiscuous */
    if (mode == 2)
        rtl_write32(REG_RCR, 0x7000A8EF);  /* accept all */
    else
        rtl_write32(REG_RCR, 0x7000208E);
    return 0;
}

static int rtl8723cs_tx_frame(wifi_interface_t* iface,
                                const uint8_t* frame, uint32_t len)
{
    (void)iface;
    if (len > 2048) return -1;
    /* Prepend 8-byte SDIO TX descriptor (simplified) */
    uint8_t* buf = kmalloc(len + 8);
    if (!buf) return -1;
    memset(buf, 0, 8);
    *(uint32_t*)buf = len;   /* TXPKTSIZE field */
    memcpy(buf + 8, frame, len);
    int ret = sdio_cmd53_write(SDIO_FUNC_WIFI, 0x8000, buf, len + 8);
    kfree(buf);
    return ret;
}

static int rtl8723cs_set_power_save(wifi_interface_t* iface, uint32_t mode)
{
    (void)iface;
    rtl_write8(0x0522, mode ? 0x0F : 0x00);
    return 0;
}

static int rtl8723cs_set_txpower(wifi_interface_t* iface, int32_t dbm)
{
    (void)iface;
    uint8_t val = (dbm < 0) ? 0 : (dbm > 20 ? 20 : (uint8_t)dbm);
    rtl_write8(0x0C80, val);
    return 0;
}

static int rtl8723cs_probe(wifi_interface_t* iface)
{
    if (sdio_host_init() < 0) return -1;

    /* Read chip ID */
    uint32_t chip_id = rtl_read32(REG_SYS_CFG);
    printk(KERN_INFO "rtl8723cs: chip_id=0x%08x\n", chip_id);

    if (rtl8723cs_load_firmware() < 0) {
        /* Continue without firmware — limited functionality */
        printk(KERN_WARN "rtl8723cs: running without firmware\n");
    }

    /* Generate or load MAC from config */
    uint8_t mac[6];
    rng_get_bytes(mac, 6);
    mac[0] &= 0xFE;  /* clear multicast bit */
    mac[0] |= 0x02;  /* set locally-administered bit */
    memcpy(iface->mac, mac, 6);

    return rtl8723cs_mac_init(mac);
}

static void rtl8723cs_remove(wifi_interface_t* iface)
{
    (void)iface;
    rtl_write32(REG_CR, 0);
    rtl_write32(REG_APS_FSMCO, 0);
}

/* ── net_if_t integration ─────────────────────────────────────── */

static net_if_t  g_wifi_netif;
static wifi_interface_t* g_wifi_iface;

static int wifi_netif_transmit(net_if_t* dev, const uint8_t* data, uint32_t len)
{
    (void)dev;
    if (!g_wifi_iface) return -1;
    /* Wrap Ethernet frame in 802.11 data frame for the AP */
    uint8_t* frame = kmalloc(len + 26);
    if (!frame) return -1;
    /* 802.11 data frame header */
    frame[0] = 0x08; frame[1] = 0x01;  /* data, ToDS=1 */
    frame[2] = 0x00; frame[3] = 0x00;  /* duration */
    memcpy(frame + 4,  g_wifi_iface->connected_bssid, 6); /* RA=BSSID */
    memcpy(frame + 10, g_wifi_iface->mac, 6);              /* TA=STA */
    memcpy(frame + 16, data, 6);                           /* DA from eth dst */
    frame[22] = 0x00; frame[23] = 0x00;                   /* seq */
    /* LLC/SNAP header */
    frame[24] = 0xAA; frame[25] = 0xAA; frame[26] = 0x03;
    frame[27] = 0x00; frame[28] = 0x00; frame[29] = 0x00;
    memcpy(frame + 30, data + 12, 2);  /* EtherType */
    memcpy(frame + 32, data + 14, len - 14);
    rtl8723cs_tx_frame(g_wifi_iface, frame, 32 + len - 14);
    kfree(frame);
    return 0;
}

static void wifi_netif_poll(net_if_t* dev)
{
    (void)dev;
    if (!g_wifi_iface) return;
    /* Poll HISR for received frames */
    uint32_t hisr = rtl_read32(REG_HISR);
    if (!(hisr & 0x02)) return;  /* RX available bit */
    rtl_write32(REG_HISR, 0x02);

    uint8_t rxbuf[2048];
    uint32_t rxlen = 0;
    uint8_t lb[2];
    sdio_cmd53_read(SDIO_FUNC_WIFI, 0x10250000, lb, 2);
    rxlen = (uint32_t)lb[0] | ((uint32_t)lb[1] << 8);
    if (rxlen < 24 || rxlen > 2048) return;
    sdio_cmd53_read(SDIO_FUNC_WIFI, 0x10250010, rxbuf, (uint8_t)rxlen);

    /* Check for EAPOL (EtherType 0x888E) */
    if (rxlen > 36 && rxbuf[30] == 0x88 && rxbuf[31] == 0x8E) {
        rtl8723cs_eapol_rx(g_wifi_iface, rxbuf, rxlen);
        return;
    }

    /* Convert 802.11 data frame → Ethernet frame and inject */
    if ((rxbuf[0] & 0x0C) == 0x08 && rxlen > 32) {
        uint8_t* eth = kmalloc(rxlen);
        if (!eth) return;
        int from_ds = !!(rxbuf[1] & 0x02);
        /* Reconstruct Ethernet: DA=frame[16], SA=frame[10 or 22] */
        memcpy(eth,     rxbuf + 16, 6);             /* DA */
        memcpy(eth + 6, from_ds ? rxbuf+10 : rxbuf+22, 6); /* SA */
        memcpy(eth + 12, rxbuf + 30, 2);            /* EtherType */
        uint32_t payload = rxlen - 32;
        memcpy(eth + 14, rxbuf + 32, payload);
        net_rx_frame(&g_wifi_netif, eth, 14 + payload);
        kfree(eth);
    }

    wifi_rx_frame(g_wifi_iface, rxbuf, rxlen, g_wifi_iface->last_rssi);
}

/* ── Public driver table and registration ─────────────────────── */

static wifi_driver_t rtl8723cs_driver = {
    .name           = "RTL8723CS",
    .probe          = rtl8723cs_probe,
    .remove         = rtl8723cs_remove,
    .tx_frame       = rtl8723cs_tx_frame,
    .set_channel    = rtl8723cs_set_channel,
    .set_mode       = rtl8723cs_set_mode,
    .start_scan     = rtl8723cs_start_scan,
    .connect        = rtl8723cs_connect,
    .disconnect     = rtl8723cs_disconnect,
    .set_power_save = rtl8723cs_set_power_save,
    .set_txpower    = rtl8723cs_set_txpower,
};

wifi_driver_t* wifi_rtl8723cs_get_driver(void) { return &rtl8723cs_driver; }

/*
 * wifi_rtl8723cs_init - Called from kmain after wifi_init().
 * Creates a wlan0 interface and registers it with the net stack.
 */
int wifi_rtl8723cs_init(void)
{
    extern wifi_interface_t* wifi_create_interface(wifi_driver_t*);
    g_wifi_iface = wifi_create_interface(&rtl8723cs_driver);
    if (!g_wifi_iface) {
        printk(KERN_WARN "wifi: RTL8723CS probe failed\n");
        return -1;
    }

    /* Register a net_if_t so DNS/HTTP/etc work over WiFi */
    memset(&g_wifi_netif, 0, sizeof(g_wifi_netif));
    memcpy(g_wifi_netif.mac,  g_wifi_iface->mac, 6);
    memcpy(g_wifi_netif.name, "wlan0", 6);
    g_wifi_netif.mtu      = 1500;
    g_wifi_netif.transmit = wifi_netif_transmit;
    g_wifi_netif.poll     = wifi_netif_poll;

    extern void net_register_if(net_if_t*);
    net_register_if(&g_wifi_netif);

    printk(KERN_INFO "wifi: wlan0 registered (RTL8723CS)\n");
    return 0;
}
