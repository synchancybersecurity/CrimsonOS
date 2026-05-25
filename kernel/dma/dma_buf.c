/* Copyright (c) 2026 SynChanCyberSecurity LLC. All Rights Reserved. */
#include <crimson/dma_buf.h>
#include <crimson/uart.h>
#include <crimson/memory.h>

#define DMA_BUF_MAX 32
#define DMA_ALIGN 4096

static dma_buf_t g_dma_table[DMA_BUF_MAX];
static uint64_t g_pool_base, g_pool_size, g_pool_used;

void dma_buf_init(uint64_t pool_base, uint64_t pool_size) {
    g_pool_base = pool_base; g_pool_size = pool_size; g_pool_used = 0;
    for (int i = 0; i < DMA_BUF_MAX; i++) g_dma_table[i].id = 0;
    uart_puts("[dma_buf] init\n");
}

dma_buf_t *dma_buf_alloc(size_t size) {
    size = (size + DMA_ALIGN - 1) & ~(DMA_ALIGN - 1);
    if (g_pool_used + size > g_pool_size) return NULL;
    for (int i = 0; i < DMA_BUF_MAX; i++) {
        if (g_dma_table[i].id == 0) {
            g_dma_table[i].id = i + 1;
            g_dma_table[i].phys = g_pool_base + g_pool_used;
            g_dma_table[i].size = size;
            g_dma_table[i].mapped = 0;
            g_pool_used += size;
            return &g_dma_table[i];
        }
    }
    return NULL;
}

void dma_buf_free(dma_buf_t *buf) {
    if (!buf || buf->id == 0) return;
    buf->id = 0; buf->phys = 0; buf->size = 0;
    buf->mapped = 0; buf->owner = NULL;
}

uint64_t dma_buf_phys(dma_buf_t *buf) { return buf ? buf->phys : 0; }
void *dma_buf_kva(dma_buf_t *buf) { return buf ? (void *)buf->phys : NULL; }

static dma_buf_t *g_user_dma[DMA_BUF_MAX];

int sys_dma_buf_create(size_t size) {
    dma_buf_t *buf = dma_buf_alloc(size);
    if (!buf) return -1;
    int fd = buf->id;
    if (fd >= DMA_BUF_MAX) { dma_buf_free(buf); return -1; }
    g_user_dma[fd] = buf;
    return fd;
}

int sys_dma_buf_destroy(int fd) {
    if (fd < 0 || fd >= DMA_BUF_MAX) return -1;
    if (g_user_dma[fd]) { dma_buf_free(g_user_dma[fd]); g_user_dma[fd] = NULL; }
    return 0;
}

int sys_dma_buf_mmap(int fd, uint64_t *out_phys) {
    if (fd < 0 || fd >= DMA_BUF_MAX || !g_user_dma[fd]) return -1;
    dma_buf_t *buf = g_user_dma[fd];
    struct task *t = sched_current_task();
    if (!t) return -1;
    if (out_phys) *out_phys = buf->phys;
    return 0;
}
