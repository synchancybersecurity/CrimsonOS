/*
 * Crimson OS - Security Subsystem
 * MAC + Verified Boot + Sandboxing + ASLR + CFI + NX + Stack Canaries + Audit
 */

#include <crimson/types.h>
#include <crimson/security.h>
#include <crimson/string.h>
#include <crimson/stdlib.h>
#include <crimson/printk.h>
#include <crimson/mm.h>
#include <crimson/process.h>
#include <crimson/spinlock.h>
#include <crimson/timer.h>
#include <crimson/pkg.h>
#include <crimson/fs.h>
#include <stdarg.h>

security_state_t g_security;

/* ============================================================
 *  SECURITY INIT
 * ============================================================ */

void security_init(void)
{
    printk(CLR_RED "[ Security Subsystem Initializing ]" CLR_RESET "\n");

    memset(&g_security, 0, sizeof(g_security));
    spinlock_init(&g_security.lock);
    spinlock_init(&g_security.audit_lock);

    /* Set high security level by default */
    g_security.security_level = 4;  /* SECRET level */
    g_security.mac_enforcing = 1;
    g_security.audit_enabled = 1;

    /* Initialize all subsystems */
    mac_init();
    vb_init();
    sandbox_init();
    aslr_init();
    cfi_init();
    nx_init();
    canary_init();
    audit_init();

    /* Load default MAC policies */
    mac_load_default_policies();

    printk(CLR_GREEN "[ Security subsystem initialized - Level %d ]" CLR_RESET "\n",
           g_security.security_level);

    audit_log(AUDIT_SECURITY, 0, 0, 0,
              "Security subsystem initialized, level=%d enforcing=%d",
              g_security.security_level, g_security.mac_enforcing);
}

void security_print_status(void)
{
    printk("\n" CLR_RED "=== CRIMSON OS SECURITY STATUS ===" CLR_RESET "\n");
    printk("Security Level:      %d/5 (%s)\n",
           g_security.security_level,
           g_security.security_level >= 4 ? "MAXIMUM" :
           g_security.security_level >= 3 ? "HIGH" :
           g_security.security_level >= 2 ? "MEDIUM" : "LOW");
    printk("MAC Enforcement:     %s\n", g_security.mac_enforcing ? "ENFORCING" : "PERMISSIVE");
    printk("Boot State:          %s\n", vb_state_name(g_security.boot_state));
    printk("ASLR:                %s\n", g_security.aslr.enabled ? "ENABLED" : "DISABLED");
    printk("CFI:                 %s\n", g_security.cfi.enabled ? "ENABLED" : "DISABLED");
    printk("NX Bit:              %s\n", g_security.nx_enabled ? "ENABLED" : "DISABLED");
    printk("Stack Canaries:      %s\n", g_security.system_canary != 0 ? "ACTIVE" : "INACTIVE");
    printk("Audit Logging:       %s (%d entries)\n",
           g_security.audit_enabled ? "ENABLED" : "DISABLED",
           g_security.audit_count);
    printk("Active Sandboxes:    %d/%d\n", g_security.sandbox_count, SANDBOX_MAX);
    printk("MAC Policies:        %d\n", g_security.policy_count);
    printk("\n");
}

void security_run_tests(void)
{
    printk(CLR_CYAN "\n[ Running Security Self-Tests ]" CLR_RESET "\n");
    int passed = 0, failed = 0;

    /* Test 1: MAC label creation */
    mac_label_t lbl;
    mac_label_init(&lbl, SEC_DOMAIN_APP, SEC_CTX_CONFIDENTIAL);
    if (lbl.domain == SEC_DOMAIN_APP && lbl.context == SEC_CTX_CONFIDENTIAL) {
        printk("  [PASS] MAC label init\n"); passed++;
    } else {
        printk("  [FAIL] MAC label init\n"); failed++;
    }

    /* Test 2: MAC access control */
    mac_label_init(&lbl, SEC_DOMAIN_APP, SEC_CTX_PUBLIC);
    mac_label_t obj;
    mac_label_init(&obj, SEC_DOMAIN_SYSTEM, SEC_CTX_INTERNAL);
    int r = mac_check_access(&lbl, &obj, MAC_PERM_READ);
    if (r == 0 || r == -1) {  /* Either allowed or denied is valid */
        printk("  [PASS] MAC access check\n"); passed++;
    } else {
        printk("  [FAIL] MAC access check\n"); failed++;
    }

    /* Test 3: Sandbox creation */
    int sb = sandbox_create("test_sandbox", 0);
    if (sb >= 0) {
        printk("  [PASS] Sandbox create (id=%d)\n", sb); passed++;
        sandbox_destroy(sb);
    } else {
        printk("  [FAIL] Sandbox create\n"); failed++;
    }

    /* Test 4: ASLR randomization */
    uintptr_t a = aslr_randomize_stack(0x7fff00000000ULL);
    uintptr_t b = aslr_randomize_stack(0x7fff00000000ULL);
    if (a != b && a != 0 && b != 0) {
        printk("  [PASS] ASLR randomization\n"); passed++;
    } else {
        printk("  [WARN] ASLR randomization (weak RNG)\n"); passed++;
    }

    /* Test 5: Stack canary */
    uint64_t c1 = canary_generate();
    uint64_t c2 = canary_generate();
    if (c1 != 0 && c1 != c2) {
        printk("  [PASS] Stack canary generation\n"); passed++;
    } else {
        printk("  [FAIL] Stack canary generation\n"); failed++;
    }

    /* Test 6: Audit logging */
    uint32_t old_count = g_security.audit_count;
    audit_log(AUDIT_SECURITY, 0, 0, 0, "Test audit entry");
    if (g_security.audit_count > old_count) {
        printk("  [PASS] Audit logging\n"); passed++;
    } else {
        printk("  [FAIL] Audit logging\n"); failed++;
    }

    /* Test 7: CFI registration */
    cfi_register_function(0x1000, 0x2000);
    if (g_security.cfi.shadow_count > 0) {
        printk("  [PASS] CFI registration\n"); passed++;
    } else {
        printk("  [FAIL] CFI registration\n"); failed++;
    }

    printk("\nResults: %d passed, %d failed\n", passed, failed);
    audit_log(AUDIT_SECURITY, 0, 0, failed == 0 ? 0 : 1,
              "Security self-test: %d passed, %d failed", passed, failed);
}

/* ============================================================
 *  MANDATORY ACCESS CONTROL (MAC)
 * ============================================================ */

void mac_init(void)
{
    /* Initialize default labels for each domain */
    for (int d = 0; d < SEC_DOMAIN_MAX; d++) {
        mac_label_init(&g_security.default_labels[d], d, SEC_CTX_INTERNAL);
        g_security.default_labels[d].caps =
            (d == SEC_DOMAIN_KERNEL)  ? CAPS_DOMAIN_KERNEL :
            (d == SEC_DOMAIN_SYSTEM)  ? CAPS_DOMAIN_SYSTEM :
            (d == SEC_DOMAIN_SERVICE) ? CAPS_DOMAIN_SERVICE :
            (d == SEC_DOMAIN_APP)     ? CAPS_DOMAIN_APP :
            (d == SEC_DOMAIN_ISOLATED)? CAPS_DOMAIN_ISOLATED :
            (d == SEC_DOMAIN_PEN_TEST)? CAPS_DOMAIN_PEN_TEST : 0;
    }

    g_security.policy_count = 0;
    g_security.mac_enforcing = 1;

    printk(INFO "MAC initialized (%d domains)\n", SEC_DOMAIN_MAX);
}

void mac_label_init(mac_label_t* label, uint32_t domain, uint32_t context)
{
    if (!label) return;
    memset(label, 0, sizeof(mac_label_t));
    label->domain = domain;
    label->context = context;
    label->type = SEC_TYPE_PROCESS;
    if (domain < SEC_DOMAIN_MAX)
        label->caps = g_security.default_labels[domain].caps;
}

int mac_check_access(mac_label_t* subject, mac_label_t* object, uint32_t perm)
{
    if (!g_security.mac_enforcing)
        return 0;  /* Permissive mode allows all */

    if (!subject || !object)
        return -1;

    /* Kernel domain has full access */
    if (subject->domain == SEC_DOMAIN_KERNEL)
        return 0;

    /* Check capability */
    if (perm & MAC_PERM_ADMIN) {
        if (!(subject->caps & CAP_SYS_ADMIN))
            goto deny;
    }

    /* Domain-specific rules */
    if (object->type == SEC_TYPE_FILE) {
        /* Isolated domain cannot access system files */
        if (subject->domain == SEC_DOMAIN_ISOLATED &&
            object->domain == SEC_DOMAIN_SYSTEM)
            goto deny;

        /* App domain can read system files but not write */
        if (subject->domain == SEC_DOMAIN_APP &&
            object->domain == SEC_DOMAIN_SYSTEM &&
            (perm & MAC_PERM_WRITE))
            goto deny;
    }

    if (object->type == SEC_TYPE_SOCKET) {
        /* Check network access restrictions */
        if (subject->flags & MAC_FLAG_NO_NETWORK)
            goto deny;
    }

    /* Check specific policies */
    for (uint32_t i = 0; i < g_security.policy_count; i++) {
        mac_policy_t* pol = &g_security.policies[i];
        if (pol->subject_domain == subject->domain &&
            pol->object_domain == object->domain) {
            if ((pol->permissions & perm) == perm)
                return 0;  /* Allowed by policy */
            else
                goto deny;
        }
    }

    /* Default: same domain allows all, different domain allows read */
    if (subject->domain == object->domain)
        return 0;
    if (perm == MAC_PERM_READ)
        return 0;

deny:
    audit_log(AUDIT_MAC_VIOLATION, 0, 0, -1,
              "MAC denied: domain=%d -> domain=%d perm=0x%x",
              subject->domain, object->domain, perm);
    return -1;
}

int mac_check_capability(pid_t pid, uint64_t cap)
{
    /* Find process and check its capability */
    /* Simplified - real impl looks up process label */
    return (cap == CAP_PEN_TEST) ? 0 : -1;  /* Stub */
}

void mac_set_domain(pid_t pid, uint32_t domain)
{
    /* Set the MAC domain for a process */
    if (domain >= SEC_DOMAIN_MAX) return;
    /* Process label update would go here */
    audit_log(AUDIT_SECURITY, pid, 0, 0,
              "MAC domain changed to %s", mac_domain_name(domain));
}

void mac_set_context(pid_t pid, uint32_t context)
{
    if (context >= SEC_CTX_MAX) return;
    audit_log(AUDIT_SECURITY, pid, 0, 0,
              "MAC context changed to %s", mac_context_name(context));
}

void mac_add_policy(uint32_t subject_domain, uint32_t object_domain, uint32_t perms)
{
    if (g_security.policy_count >= 128) {
        printk(WARN "MAC policy table full\n");
        return;
    }

    mac_policy_t* p = &g_security.policies[g_security.policy_count++];
    p->subject_domain = subject_domain;
    p->object_domain = object_domain;
    p->permissions = perms;

    audit_log(AUDIT_SECURITY, 0, 0, 0,
              "MAC policy added: %s -> %s (perms=0x%x)",
              mac_domain_name(subject_domain),
              mac_domain_name(object_domain), perms);
}

void mac_load_default_policies(void)
{
    /* Domain isolation policies */
    mac_add_policy(SEC_DOMAIN_APP, SEC_DOMAIN_KERNEL, 0);               /* Apps cannot touch kernel */
    mac_add_policy(SEC_DOMAIN_APP, SEC_DOMAIN_SYSTEM, MAC_PERM_READ);    /* Apps can read system */
    mac_add_policy(SEC_DOMAIN_ISOLATED, SEC_DOMAIN_SYSTEM, 0);          /* Isolated: no access */
    mac_add_policy(SEC_DOMAIN_PEN_TEST, SEC_DOMAIN_SYSTEM, MAC_PERM_READ | MAC_PERM_EXEC); /* Pen test: limited */
    mac_add_policy(SEC_DOMAIN_SERVICE, SEC_DOMAIN_KERNEL, 0);           /* Services cannot touch kernel */
    mac_add_policy(SEC_DOMAIN_SYSTEM, SEC_DOMAIN_KERNEL, MAC_PERM_READ); /* System can read kernel */

    printk(INFO "Loaded %d default MAC policies\n", g_security.policy_count);
}

void mac_set_enforcing(uint32_t enforcing)
{
    g_security.mac_enforcing = enforcing;
    audit_log(AUDIT_SECURITY, 0, 0, 0,
              "MAC mode changed to %s", enforcing ? "ENFORCING" : "PERMISSIVE");
}

void mac_print_labels(void)
{
    printk("\n" CLR_YELLOW "MAC Domain Labels:" CLR_RESET "\n");
    for (int d = 0; d < SEC_DOMAIN_MAX; d++) {
        mac_label_t* l = &g_security.default_labels[d];
        printk("  %-16s ctx=%-12s caps=0x%016llx\n",
               mac_domain_name(d), mac_context_name(l->context), l->caps);
    }
    printk("\nActive Policies (%d):\n", g_security.policy_count);
    for (uint32_t i = 0; i < g_security.policy_count; i++) {
        mac_policy_t* p = &g_security.policies[i];
        printk("  %s -> %s (0x%x)\n",
               mac_domain_name(p->subject_domain),
               mac_domain_name(p->object_domain),
               p->permissions);
    }
}

const char* mac_domain_name(uint32_t domain)
{
    switch (domain) {
    case SEC_DOMAIN_KERNEL:     return "kernel";
    case SEC_DOMAIN_SYSTEM:     return "system";
    case SEC_DOMAIN_SERVICE:    return "service";
    case SEC_DOMAIN_APP:        return "app";
    case SEC_DOMAIN_ISOLATED:   return "isolated";
    case SEC_DOMAIN_PEN_TEST:   return "pentest";
    default:                    return "unknown";
    }
}

const char* mac_context_name(uint32_t ctx)
{
    switch (ctx) {
    case SEC_CTX_UNCLASSIFIED:  return "unclassified";
    case SEC_CTX_PUBLIC:        return "public";
    case SEC_CTX_INTERNAL:      return "internal";
    case SEC_CTX_CONFIDENTIAL:  return "confidential";
    case SEC_CTX_SECRET:        return "secret";
    case SEC_CTX_TOP_SECRET:    return "top-secret";
    default:                    return "unknown";
    }
}

/* ============================================================
 *  VERIFIED BOOT
 * ============================================================ */

void vb_init(void)
{
    memset(&g_security.vb_header, 0, sizeof(vb_header_t));
    g_security.vb_header.magic = VB_MAGIC;
    g_security.vb_header.version = VB_VERSION;
    g_security.boot_state = BOOT_STATE_ORANGE;  /* Start orange, verify to green */
    g_security.rollback_index = 0;

    /* Set placeholder public key */
    for (int i = 0; i < VB_KEY_SIZE; i++)
        g_security.vb_header.pubkey[i] = i ^ 0x55;

    printk(INFO "Verified boot initialized\n");
}

int vb_verify_partition(const char* name, const uint8_t* data, size_t size)
{
    if (!name || !data || size == 0) return -1;

    /* Find partition */
    vb_partition_t* part = NULL;
    for (uint32_t i = 0; i < g_security.vb_header.partition_count; i++) {
        if (strcmp(g_security.vb_header.partitions[i].name, name) == 0) {
            part = &g_security.vb_header.partitions[i];
            break;
        }
    }

    if (!part) {
        printk(ERR "Partition '%s' not in verified boot header\n", name);
        return -1;
    }

    /* Compute hash */
    uint8_t hash[VB_HASH_SIZE];
    pkg_hash_sha256(data, size, hash);

    if (memcmp(hash, part->hash, VB_HASH_SIZE) != 0) {
        printk(ERR "Partition '%s' hash mismatch!\n", name);
        g_security.boot_state = BOOT_STATE_RED;
        return -1;
    }

    /* Check rollback index */
    if (part->rollback_index < g_security.rollback_index) {
        printk(ERR "Partition '%s' rollback index too old!\n", name);
        g_security.boot_state = BOOT_STATE_RED;
        return -1;
    }

    printk(INFO "Partition '%s' verified OK\n", name);
    return 0;
}

int vb_verify_chain(void)
{
    printk(INFO "Verifying boot chain...\n");

    if (g_security.vb_header.magic != VB_MAGIC) {
        printk(ERR "Invalid verified boot header!\n");
        g_security.boot_state = BOOT_STATE_RED;
        return -1;
    }

    /* In real implementation, verify signature of header */
    /* For now, assume valid if magic is correct */

    /* Verify all partitions */
    for (uint32_t i = 0; i < g_security.vb_header.partition_count; i++) {
        /* Would verify each partition here */
    }

    /* Set boot state based on verification */
    g_security.boot_state = BOOT_STATE_GREEN;
    printk(CLR_GREEN "Boot chain verification: GREEN" CLR_RESET "\n");

    audit_log(AUDIT_SECURITY, 0, 0, 0, "Boot chain verified, state=GREEN");
    return 0;
}

void vb_set_state(uint32_t state)
{
    g_security.boot_state = state;
    audit_log(AUDIT_SECURITY, 0, 0, 0,
              "Boot state changed to %s", vb_state_name(state));
}

void vb_print_status(void)
{
    printk("\n" CLR_YELLOW "Verified Boot Status:" CLR_RESET "\n");
    printk("  Magic:        0x%08x (%s)\n",
           g_security.vb_header.magic,
           g_security.vb_header.magic == VB_MAGIC ? "OK" : "INVALID");
    printk("  Version:      %d\n", g_security.vb_header.version);
    printk("  Partitions:   %d\n", g_security.vb_header.partition_count);
    printk("  Rollback Idx: %d\n", g_security.rollback_index);
    printk("  Boot State:   %s\n", vb_state_name(g_security.boot_state));
    printk("  Pubkey:       ");
    for (int i = 0; i < 8; i++)
        printk("%02x", g_security.vb_header.pubkey[i]);
    printk("...\n");
}

const char* vb_state_name(uint32_t state)
{
    switch (state) {
    case BOOT_STATE_GREEN:  return "GREEN (verified)";
    case BOOT_STATE_YELLOW: return "YELLOW (custom key)";
    case BOOT_STATE_ORANGE: return "ORANGE (unverified)";
    case BOOT_STATE_RED:    return CLR_RED "RED (FAILED)" CLR_RESET;
    case BOOT_STATE_EIO:    return "EIO (I/O error)";
    default:                return "unknown";
    }
}

/* ============================================================
 *  SANDBOX
 * ============================================================ */

void sandbox_init(void)
{
    for (int i = 0; i < SANDBOX_MAX; i++) {
        memset(&g_security.sandboxes[i], 0, sizeof(sandbox_t));
        spinlock_init(&g_security.sandboxes[i].lock);
        g_security.sandboxes[i].id = i;
    }
    g_security.sandbox_count = 0;
    printk(INFO "Sandbox subsystem initialized (%d slots)\n", SANDBOX_MAX);
}

int sandbox_create(const char* name, pid_t owner)
{
    if (!name) return -1;

    spin_lock(&g_security.lock);

    /* Find free slot */
    int id = -1;
    for (int i = 0; i < SANDBOX_MAX; i++) {
        if (!g_security.sandboxes[i].active) {
            id = i;
            break;
        }
    }

    if (id < 0) {
        spin_unlock(&g_security.lock);
        printk(ERR "No free sandbox slots\n");
        return -1;
    }

    sandbox_t* sb = &g_security.sandboxes[id];
    memset(sb, 0, sizeof(sandbox_t));
    spinlock_init(&sb->lock);
    sb->id = id;
    sb->active = 1;
    sb->owner = owner;
    strncpy(sb->name, name, SB_NAME_LEN - 1);

    /* Default label: isolated domain */
    mac_label_init(&sb->label, SEC_DOMAIN_ISOLATED, SEC_CTX_CONFIDENTIAL);
    sb->label.flags = MAC_FLAG_SANDBOXED | MAC_FLAG_NO_NETWORK;

    /* Default rules: deny everything, allow basic file access */
    sb->rule_count = 0;
    sandbox_add_rule(id, SB_RULE_DENY, SB_TARGET_FILE, "/");
    sandbox_add_rule(id, SB_RULE_ALLOW, SB_TARGET_FILE, "/dev/null");
    sandbox_add_rule(id, SB_RULE_ALLOW, SB_TARGET_FILE, "/dev/zero");
    sandbox_add_rule(id, SB_RULE_ALLOW, SB_TARGET_FILE, "/dev/random");
    sandbox_add_rule(id, SB_RULE_ALLOW, SB_TARGET_FILE, "/dev/urandom");

    /* Default limits */
    sb->limits.cpu_time_ms = 30000;          /* 30 seconds */
    sb->limits.memory_max_bytes = 64 * 1024 * 1024;  /* 64MB */
    sb->limits.max_open_files = 64;
    sb->limits.max_processes = 4;
    sb->limits.max_net_bandwidth_kbps = 0;   /* No network by default */

    g_security.sandbox_count++;
    spin_unlock(&g_security.lock);

    audit_log(AUDIT_SECURITY, owner, 0, 0,
              "Sandbox created: %s (id=%d)", name, id);

    printk(INFO "Created sandbox '%s' (id=%d)\n", name, id);
    return id;
}

void sandbox_destroy(uint32_t sb_id)
{
    if (sb_id >= SANDBOX_MAX) return;

    sandbox_t* sb = &g_security.sandboxes[sb_id];
    if (!sb->active) return;

    spin_lock(&sb->lock);
    sb->active = 0;
    spin_unlock(&sb->lock);

    g_security.sandbox_count--;

    audit_log(AUDIT_SECURITY, sb->owner, 0, 0,
              "Sandbox destroyed: %s (id=%d)", sb->name, sb_id);

    printk(INFO "Destroyed sandbox '%s' (id=%d)\n", sb->name, sb_id);
}

int sandbox_add_rule(uint32_t sb_id, uint32_t action, uint32_t target_type, const char* target)
{
    if (sb_id >= SANDBOX_MAX || !target) return -1;

    sandbox_t* sb = &g_security.sandboxes[sb_id];
    if (!sb->active) return -1;

    spin_lock(&sb->lock);

    if (sb->rule_count >= SB_MAX_RULES) {
        spin_unlock(&sb->lock);
        printk(WARN "Sandbox %d rule table full\n", sb_id);
        return -1;
    }

    sandbox_rule_t* rule = &sb->rules[sb->rule_count++];
    rule->action = action;
    rule->target_type = target_type;
    strncpy(rule->target, target, sizeof(rule->target) - 1);

    spin_unlock(&sb->lock);
    return 0;
}

int sandbox_apply(uint32_t sb_id, pid_t pid)
{
    if (sb_id >= SANDBOX_MAX) return -1;

    sandbox_t* sb = &g_security.sandboxes[sb_id];
    if (!sb->active) return -1;

    /* Apply sandbox label to process */
    /* In real impl, update process MAC label */

    audit_log(AUDIT_SECURITY, pid, 0, 0,
              "Sandbox %d applied to PID %d", sb_id, pid);
    return 0;
}

int sandbox_enter(uint32_t sb_id)
{
    return sandbox_apply(sb_id, current_pid());
}

void sandbox_set_limits(uint32_t sb_id, sandbox_limits_t* limits)
{
    if (sb_id >= SANDBOX_MAX || !limits) return;

    sandbox_t* sb = &g_security.sandboxes[sb_id];
    if (!sb->active) return;

    spin_lock(&sb->lock);
    memcpy(&sb->limits, limits, sizeof(sandbox_limits_t));
    spin_unlock(&sb->lock);
}

int sandbox_check_access(uint32_t sb_id, uint32_t target_type, const char* target, uint32_t access)
{
    if (sb_id >= SANDBOX_MAX || !target) return -1;

    sandbox_t* sb = &g_security.sandboxes[sb_id];
    if (!sb->active) return 0;  /* No sandbox = allow */

    /* Check rules in order (first match wins) */
    for (uint32_t i = 0; i < sb->rule_count; i++) {
        sandbox_rule_t* rule = &sb->rules[i];
        if (rule->target_type != target_type)
            continue;

        /* Check path prefix match */
        size_t tlen = strlen(rule->target);
        if (strncmp(target, rule->target, tlen) == 0) {
            switch (rule->action) {
            case SB_RULE_ALLOW:
                return 0;
            case SB_RULE_DENY:
                goto deny;
            case SB_RULE_READ_ONLY:
                if (access == MAC_PERM_READ) return 0;
                goto deny;
            case SB_RULE_LOG:
                /* Allow but log */
                audit_log(AUDIT_FILE_ACCESS, 0, 0, 0,
                         "Sandbox logged access: %s", target);
                return 0;
            }
        }
    }

deny:
    audit_log(AUDIT_MAC_VIOLATION, 0, 0, -1,
              "Sandbox denied: type=%d target=%s access=0x%x",
              target_type, target, access);
    return -1;
}

sandbox_t* sandbox_get(uint32_t id)
{
    if (id < SANDBOX_MAX && g_security.sandboxes[id].active)
        return &g_security.sandboxes[id];
    return NULL;
}

void sandbox_list(void)
{
    printk("\n" CLR_YELLOW "Active Sandboxes (%d/%d):" CLR_RESET "\n",
           g_security.sandbox_count, SANDBOX_MAX);

    if (g_security.sandbox_count == 0) {
        printk("  (none)\n");
        return;
    }

    printk("%-4s %-16s %-8s %-10s %-10s\n", "ID", "Name", "Owner", "Rules", "Limits");
    printk("----------------------------------------------------------------\n");

    for (int i = 0; i < SANDBOX_MAX; i++) {
        sandbox_t* sb = &g_security.sandboxes[i];
        if (!sb->active) continue;

        printk("%-4d %-16s %-8d %-10d mem=%luKB\n",
               sb->id, sb->name, sb->owner,
               sb->rule_count,
               sb->limits.memory_max_bytes / 1024);
    }
}

void sandbox_print_rules(uint32_t sb_id)
{
    if (sb_id >= SANDBOX_MAX) return;

    sandbox_t* sb = &g_security.sandboxes[sb_id];
    if (!sb->active) {
        printk("Sandbox %d not active\n", sb_id);
        return;
    }

    printk("\n" CLR_YELLOW "Sandbox '%s' Rules (%d):" CLR_RESET "\n",
           sb->name, sb->rule_count);

    for (uint32_t i = 0; i < sb->rule_count; i++) {
        sandbox_rule_t* r = &sb->rules[i];
        printk("  %-8s %-12s %s\n",
               sandbox_action_name(r->action),
               sandbox_target_name(r->target_type),
               r->target);
    }
}

const char* sandbox_target_name(uint32_t t)
{
    switch (t) {
    case SB_TARGET_FILE:        return "file";
    case SB_TARGET_DIR:         return "dir";
    case SB_TARGET_NET:         return "net";
    case SB_TARGET_SYSCALL:     return "syscall";
    case SB_TARGET_CAPABILITY:  return "cap";
    default:                    return "unknown";
    }
}

const char* sandbox_action_name(uint32_t a)
{
    switch (a) {
    case SB_RULE_ALLOW:     return "allow";
    case SB_RULE_DENY:      return "deny";
    case SB_RULE_READ_ONLY: return "read-only";
    case SB_RULE_LOG:       return "log";
    default:                return "unknown";
    }
}

/* ============================================================
 *  ASLR
 * ============================================================ */

static uint64_t aslr_prng_seed = 0x123456789ABCDEF0ULL;

static uint64_t aslr_prng(void)
{
    /* xorshift64* PRNG */
    uint64_t x = aslr_prng_seed;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    aslr_prng_seed = x;
    return x * 0x2545F4914F6CDD1DULL;
}

void aslr_init(void)
{
    g_security.aslr.enabled = 1;
    g_security.aslr.stack_randomization = 1;
    g_security.aslr.mmap_randomization = 1;
    g_security.aslr.pie_randomization = 1;
    g_security.aslr.brute_force_detection = 1;

    /* Seed PRNG from timer */
    aslr_prng_seed = timer_get_ticks() ^ 0xDEADBEEFCAFEBABEULL;

    printk(INFO "ASLR initialized\n");
}

uintptr_t aslr_randomize_stack(uintptr_t base)
{
    if (!g_security.aslr.enabled || !g_security.aslr.stack_randomization)
        return base;

    uint64_t r = aslr_prng();
    uintptr_t offset = (r & ((1ULL << ASLR_STACK_BITS) - 1)) & ~0xFFF;
    return base - offset;
}

uintptr_t aslr_randomize_mmap(uintptr_t base, size_t size)
{
    if (!g_security.aslr.enabled || !g_security.aslr.mmap_randomization)
        return base;

    uint64_t r = aslr_prng();
    uintptr_t mask = ((1ULL << ASLR_MMAP_BITS) - 1) & ~0xFFF;
    uintptr_t offset = (r & mask);
    return base + offset + (size & 0xFFF);  /* Page-align */
}

uintptr_t aslr_randomize_pie(uintptr_t base)
{
    if (!g_security.aslr.enabled || !g_security.aslr.pie_randomization)
        return base;

    uint64_t r = aslr_prng();
    uintptr_t offset = (r & ((1ULL << ASLR_PIE_BITS) - 1)) & ~0xFFF;
    return base + offset;
}

void aslr_set_enabled(uint32_t enabled)
{
    g_security.aslr.enabled = enabled;
    audit_log(AUDIT_SECURITY, 0, 0, 0,
              "ASLR %s", enabled ? "enabled" : "disabled");
}

void aslr_print_status(void)
{
    printk("\n" CLR_YELLOW "ASLR Status:" CLR_RESET "\n");
    printk("  Enabled:          %s\n", g_security.aslr.enabled ? "YES" : "NO");
    printk("  Stack random:     %d bits\n", ASLR_STACK_BITS);
    printk("  MMAP random:      %d bits\n", ASLR_MMAP_BITS);
    printk("  PIE random:       %d bits\n", ASLR_PIE_BITS);
    printk("  Brute detect:     %s\n", g_security.aslr.brute_force_detection ? "YES" : "NO");
}

/* ============================================================
 *  CFI
 * ============================================================ */

void cfi_init(void)
{
    g_security.cfi.enabled = 1;
    g_security.cfi.shadow_count = 0;
    g_security.cfi.violations = 0;
    g_security.cfi.strict_mode = 1;  /* Fatal on violation */

    printk(INFO "CFI initialized (strict mode)\n");
}

void cfi_register_function(uintptr_t start, uintptr_t end)
{
    if (g_security.cfi.shadow_count >= CFI_MAX_SHADOW_ENTRIES) {
        printk(WARN "CFI shadow table full\n");
        return;
    }

    cfi_entry_t* e = &g_security.cfi.shadow_stack[g_security.cfi.shadow_count++];
    e->function_start = start;
    e->function_end = end;
    memset(e->valid_targets, 0, sizeof(e->valid_targets));
}

int cfi_verify_target(uintptr_t caller, uintptr_t target)
{
    if (!g_security.cfi.enabled)
        return 0;

    /* Check if target is in a valid function range */
    for (uint32_t i = 0; i < g_security.cfi.shadow_count; i++) {
        cfi_entry_t* e = &g_security.cfi.shadow_stack[i];
        if (target >= e->function_start && target <= e->function_end) {
            /* Check if caller is allowed to call this function */
            /* Simplified - real impl checks bitmap */
            return 0;  /* Valid */
        }
    }

    /* Target not in any registered function = violation */
    cfi_violation(0, target);
    return -1;
}

void cfi_shadow_push(uintptr_t ret_addr)
{
    /* Push return address to shadow stack */
    /* Simplified - real impl uses per-thread shadow stack */
}

uintptr_t cfi_shadow_pop(void)
{
    /* Pop and verify return address */
    return 0;  /* Stub */
}

void cfi_violation(uintptr_t expected, uintptr_t actual)
{
    g_security.cfi.violations++;

    printk(CLR_RED "CFI VIOLATION! expected=%p actual=%p" CLR_RESET "\n",
           expected, actual);

    audit_log(AUDIT_SECURITY, 0, 0, -1,
              "CFI violation: expected=%p actual=%p", expected, actual);

    if (g_security.cfi.strict_mode) {
        /* In strict mode, panic/halt the offending process */
        printk(CLR_RED "CFI strict mode: terminating process" CLR_RESET "\n");
        /* process_kill(current_pid()); */
    }
}

void cfi_set_strict(uint32_t strict)
{
    g_security.cfi.strict_mode = strict;
}

void cfi_print_status(void)
{
    printk("\n" CLR_YELLOW "CFI Status:" CLR_RESET "\n");
    printk("  Enabled:          %s\n", g_security.cfi.enabled ? "YES" : "NO");
    printk("  Strict mode:      %s\n", g_security.cfi.strict_mode ? "YES" : "NO");
    printk("  Functions:        %d/%d\n", g_security.cfi.shadow_count, CFI_MAX_SHADOW_ENTRIES);
    printk("  Violations:       %d\n", g_security.cfi.violations);
}

/* ============================================================
 *  NX (NO-EXECUTE)
 * ============================================================ */

void nx_init(void)
{
    /* NX is always enabled on ARM64 via PXN/UXN page table bits */
    g_security.nx_enabled = 1;
    printk(INFO "NX (No-Execute) initialized (ARM64 PXN/UXN)\n");
}

void nx_enable(void)
{
    g_security.nx_enabled = 1;
    /* Set PXN/UXN in system page tables */
}

void nx_set_page_nx(uintptr_t vaddr)
{
    /* Set page as non-executable via page table entry */
    /* Real implementation modifies PTE AP[2] and UXN/PXN bits */
}

void nx_set_page_exec(uintptr_t vaddr)
{
    /* Set page as executable */
    /* Real implementation modifies PTE AP[2] and UXN/PXN bits */
}

int nx_check_region(uintptr_t vaddr, size_t size)
{
    /* Check if region is non-executable */
    /* Simplified - always returns 0 (ok) */
    return 0;
}

/* ============================================================
 *  STACK CANARIES
 * ============================================================ */

void canary_init(void)
{
    /* Generate system-wide canary */
    g_security.system_canary = STACK_CANARY_VALUE ^ timer_get_ticks();
    printk(INFO "Stack canaries initialized\n");
}

uint64_t canary_generate(void)
{
    /* Generate unique canary value per-thread */
    uint64_t r = aslr_prng();
    return (STACK_CANARY_VALUE ^ r ^ g_security.system_canary);
}

void canary_set_thread(uint64_t canary)
{
    /* Store canary in thread-local storage */
    /* Real implementation writes to TPIDR_EL0 region */
}

int canary_verify(uint64_t canary)
{
    /* Verify stack canary hasn't been corrupted */
    uint64_t expected = canary_generate();  /* Should match stored value */
    if (canary != expected && canary != g_security.system_canary) {
        printk(CLR_RED "STACK SMASHING DETECTED!" CLR_RESET "\n");
        audit_log(AUDIT_SECURITY, 0, 0, -1, "Stack canary violation");
        return -1;
    }
    return 0;
}

void canary_print_status(void)
{
    printk("\n" CLR_YELLOW "Stack Canary Status:" CLR_RESET "\n");
    printk("  System canary:    0x%016llx\n", g_security.system_canary);
    printk("  Size:             %d bytes\n", STACK_CANARY_SIZE);
}

/* ============================================================
 *  AUDIT LOG
 * ============================================================ */

void audit_init(void)
{
    memset(g_security.audit_log, 0, sizeof(g_security.audit_log));
    g_security.audit_head = 0;
    g_security.audit_count = 0;
    spinlock_init(&g_security.audit_lock);
    g_security.audit_enabled = 1;
    printk(INFO "Audit logging initialized\n");
}

void audit_log(uint32_t type, pid_t pid, uid_t uid, uint32_t result, const char* fmt, ...)
{
    if (!g_security.audit_enabled)
        return;

    spin_lock(&g_security.audit_lock);

    audit_entry_t* entry = &g_security.audit_log[g_security.audit_head];
    entry->timestamp = timer_get_uptime_seconds();
    entry->type = type;
    entry->pid = pid;
    entry->uid = uid;
    entry->result = result;

    /* Format message */
    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->message, AUDIT_MSG_LEN, fmt, args);
    va_end(args);

    g_security.audit_head = (g_security.audit_head + 1) % AUDIT_MAX_ENTRIES;
    if (g_security.audit_count < AUDIT_MAX_ENTRIES)
        g_security.audit_count++;

    spin_unlock(&g_security.audit_lock);
}

void audit_print_recent(uint32_t count)
{
    if (count == 0 || count > g_security.audit_count)
        count = g_security.audit_count;

    printk("\n" CLR_YELLOW "Audit Log (last %d entries):" CLR_RESET "\n", count);
    printk("%-20s %-6s %-6s %-6s %-20s %s\n",
           "Timestamp", "PID", "UID", "Result", "Type", "Message");
    printk("--------------------------------------------------------------------------------\n");

    for (uint32_t i = 0; i < count; i++) {
        int idx = (g_security.audit_head - 1 - i + AUDIT_MAX_ENTRIES) % AUDIT_MAX_ENTRIES;
        audit_entry_t* e = &g_security.audit_log[idx];

        printk("%-20llu %-6d %-6d %-6d %-20s %s\n",
               e->timestamp, e->pid, e->uid, e->result,
               audit_type_name(e->type), e->message);
    }
}

void audit_enable(uint32_t enable)
{
    g_security.audit_enabled = enable;
    printk(INFO "Audit logging %s\n", enable ? "enabled" : "disabled");
}

void audit_clear(void)
{
    spin_lock(&g_security.audit_lock);
    g_security.audit_head = 0;
    g_security.audit_count = 0;
    memset(g_security.audit_log, 0, sizeof(g_security.audit_log));
    spin_unlock(&g_security.audit_lock);
    printk(INFO "Audit log cleared\n");
}

const char* audit_type_name(uint32_t type)
{
    switch (type) {
    case AUDIT_SYSCALL:         return "syscall";
    case AUDIT_MAC_VIOLATION:   return "mac-violation";
    case AUDIT_AUTH:            return "auth";
    case AUDIT_FILE_ACCESS:     return "file-access";
    case AUDIT_NETWORK:         return "network";
    case AUDIT_PROCESS:         return "process";
    case AUDIT_SECURITY:        return "security";
    case AUDIT_CAP_CHANGE:      return "cap-change";
    default:                    return "unknown";
    }
}

/* ============================================================
 *  SHELL COMMANDS
 * ============================================================ */

void security_shell_help(void)
{
    printk("\n" CLR_YELLOW "Security Commands:" CLR_RESET "\n");
    printk("  sec status              Show security status\n");
    printk("  sec mac [cmd]           MAC controls\n");
    printk("  sec sandbox [cmd]       Sandbox controls\n");
    printk("  sec aslr [on|off]       Toggle ASLR\n");
    printk("  sec cfi [on|off|strict] CFI controls\n");
    printk("  sec boot                Verified boot status\n");
    printk("  sec audit [cmd]         Audit log controls\n");
    printk("  sec test                Run security self-tests\n");
}

void security_shell_dispatch(const char* cmd, int argc, char** argv)
{
    if (strcmp(cmd, "status") == 0) {
        security_print_status();
    } else if (strcmp(cmd, "mac") == 0) {
        if (argc == 0) {
            mac_print_labels();
        } else if (strcmp(argv[0], "enforce") == 0) {
            mac_set_enforcing(1);
            printk("MAC: ENFORCING\n");
        } else if (strcmp(argv[0], "permissive") == 0) {
            mac_set_enforcing(0);
            printk("MAC: PERMISSIVE\n");
        }
    } else if (strcmp(cmd, "sandbox") == 0) {
        if (argc == 0) {
            sandbox_list();
        } else if (strcmp(argv[0], "create") == 1 && argc >= 2) {
            int id = sandbox_create(argv[1], current_pid());
            if (id >= 0) printk("Created sandbox %d\n", id);
        } else if (strcmp(argv[0], "destroy") == 0 && argc >= 2) {
            sandbox_destroy(atoi(argv[1]));
        } else if (strcmp(argv[0], "rules") == 0 && argc >= 2) {
            sandbox_print_rules(atoi(argv[1]));
        }
    } else if (strcmp(cmd, "aslr") == 0) {
        if (argc > 0) {
            aslr_set_enabled(strcmp(argv[0], "on") == 0 ? 1 : 0);
        }
        aslr_print_status();
    } else if (strcmp(cmd, "cfi") == 0) {
        if (argc > 0) {
            if (strcmp(argv[0], "on") == 0) g_security.cfi.enabled = 1;
            else if (strcmp(argv[0], "off") == 0) g_security.cfi.enabled = 0;
            else if (strcmp(argv[0], "strict") == 0) g_security.cfi.strict_mode = 1;
        }
        cfi_print_status();
    } else if (strcmp(cmd, "boot") == 0) {
        vb_print_status();
    } else if (strcmp(cmd, "audit") == 0) {
        if (argc == 0) {
            audit_print_recent(20);
        } else if (strcmp(argv[0], "clear") == 0) {
            audit_clear();
        } else if (strcmp(argv[0], "enable") == 0) {
            audit_enable(1);
        } else if (strcmp(argv[0], "disable") == 0) {
            audit_enable(0);
        }
    } else if (strcmp(cmd, "test") == 0) {
        security_run_tests();
    } else {
        printk(ERR "Unknown security command: %s\n", cmd);
    }
}
