/*
 * ShrikeVault — wallet firmware entry point (P1).
 *
 * What works in this build:
 *   • device_key: on first boot, generates a 256-bit root from the ESP32-S3
 *     hardware RNG, persists it in NVS; on subsequent boots, reads it back.
 *   • Console over the built-in USB Serial/JTAG with these commands:
 *       dk_status      — is a device key provisioned? show its first/last bytes
 *                        (for sanity-checking persistence across boots)
 *       dk_provision   — create a key (refuses if one already exists)
 *       dk_wipe        — DESTROY the device key (factory reset; cannot be undone)
 *
 * What's coming (next iterations):
 *   • bip39_bip32: derive BIP-39 mnemonic + BIP-32 master from device_key
 *   • eth_tx: secp256k1 signing, RLP, EIP-1559, EIP-55
 *   • wallet_transport: TinyUSB CDC-ACM framed protocol
 *
 * This is a DEBUG build — Secure Boot v2 / Flash Encryption / encrypted NVS
 * land in the release build profile in P4. Do not use this for real funds.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_chip_info.h"

#include "device_key.h"
#include "wallet_keys.h"
#include "eth_tx.h"

static const char *TAG = "shrikevault";

/* ------------------------- console: dk_status --------------------------- */

static int cmd_dk_status(int argc, char **argv)
{
    bool present = device_key_provisioned();
    printf("device key provisioned: %s\n", present ? "YES" : "no (first init)");
    if (present) {
        uint8_t k[DEVICE_KEY_BYTES];
        if (device_key_get(k) == ESP_OK) {
            /* Show only the first 4 and last 4 bytes so the user can recognise
             * "same key as last boot" without us pasting the whole secret on
             * the console. (This is debug-only convenience; will be removed
             * in release builds where we never log secret bytes at all.) */
            printf("  fingerprint: %02x%02x%02x%02x ... %02x%02x%02x%02x\n",
                   k[0], k[1], k[2], k[3], k[28], k[29], k[30], k[31]);
        }
        device_key_memzero(k, sizeof(k));
    }
    return 0;
}

/* ------------------------- console: dk_provision ------------------------ */

static int cmd_dk_provision(int argc, char **argv)
{
    uint8_t k[DEVICE_KEY_BYTES];
    esp_err_t err = device_key_provision(k);
    if (err == ESP_ERR_INVALID_STATE) { printf("already provisioned — use dk_wipe first if you really want to rotate.\n"); return 1; }
    if (err != ESP_OK)                { printf("provision failed: %s\n", esp_err_to_name(err)); return 1; }
    printf("provisioned. fingerprint: %02x%02x%02x%02x ... %02x%02x%02x%02x\n",
           k[0], k[1], k[2], k[3], k[28], k[29], k[30], k[31]);
    device_key_memzero(k, sizeof(k));
    printf("next step (coming): dk_show_mnemonic  to derive & display the BIP-39 backup\n");
    return 0;
}

/* ------------------------- console: dk_wipe ----------------------------- */

static int cmd_dk_wipe(int argc, char **argv)
{
    /* Require a magic word to avoid accidental wipes. */
    if (argc < 2 || strcmp(argv[1], "CONFIRM") != 0) {
        printf("DESTRUCTIVE.  Type:  dk_wipe CONFIRM   to actually erase the device key.\n");
        printf("After wiping, the funds are only recoverable via the BIP-39 mnemonic\n");
        printf("you wrote down at first init. If you did not write it down — STOP.\n");
        return 1;
    }
    esp_err_t err = device_key_wipe();
    printf("dk_wipe: %s\n", err == ESP_OK ? "OK" : esp_err_to_name(err));
    return 0;
}

/* ------------------------- console: show_mnemonic ----------------------- */

static int cmd_show_mnemonic(int argc, char **argv)
{
    if (!device_key_provisioned()) {
        printf("no device key yet — run 'dk_provision' first.\n");
        return 1;
    }
    if (argc < 2 || strcmp(argv[1], "CONFIRM") != 0) {
        printf("This shows your 24-word BIP-39 mnemonic on the console.\n");
        printf("ANYONE with the words has full access to your funds.\n");
        printf("Type:  show_mnemonic CONFIRM   if you're ready to write them down.\n");
        return 1;
    }
    char mnemonic[WALLET_MNEMONIC_MAX];
    esp_err_t err = wallet_derive_mnemonic(mnemonic);
    if (err != ESP_OK) { printf("derive failed: %s\n", esp_err_to_name(err)); return 1; }

    printf("\n========== BIP-39 MNEMONIC (write this down, then never share it) ==========\n\n");
    /* Print as numbered 4-column grid for legibility. */
    char *saveptr = NULL;
    char *tok = strtok_r(mnemonic, " ", &saveptr);
    int i = 1;
    while (tok) {
        printf("  %2d. %-10s", i, tok);
        if (i % 4 == 0) printf("\n");
        tok = strtok_r(NULL, " ", &saveptr);
        i++;
    }
    if ((i - 1) % 4 != 0) printf("\n");
    printf("\n============================================================================\n");
    device_key_memzero(mnemonic, sizeof(mnemonic));
    return 0;
}

/* ------------------------- console: addr -------------------------------- */

static int cmd_addr(int argc, char **argv)
{
    if (!device_key_provisioned()) {
        printf("no device key yet — run 'dk_provision' first.\n");
        return 1;
    }
    uint32_t index = (argc >= 2) ? (uint32_t)strtoul(argv[1], NULL, 10) : 0;
    char addr[WALLET_ETH_ADDR_LEN];
    esp_err_t err = wallet_get_eth_address(index, addr);
    if (err != ESP_OK) { printf("derive failed: %s\n", esp_err_to_name(err)); return 1; }
    printf("m/44'/60'/0'/0/%lu  ->  %s\n", (unsigned long)index, addr);
    return 0;
}

/* --------- hex helpers (small, no deps) ------------------------------- */

static int from_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a hex string (with or without "0x" prefix) into out_buf.
 * Returns bytes written on success, -1 on error. */
static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_max) {
    if (!hex) return -1;
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    size_t hlen = strlen(hex);
    if (hlen & 1) return -1;
    size_t n = hlen / 2;
    if (n > out_max) return -1;
    for (size_t i = 0; i < n; i++) {
        int hi = from_hex_nibble(hex[2 * i]);
        int lo = from_hex_nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

static void print_hex(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) printf("%02x", buf[i]);
}

static void print_signature(const uint8_t sig[65]) {
    printf("  r           : 0x"); print_hex(sig,        32); printf("\n");
    printf("  s           : 0x"); print_hex(sig + 32,   32); printf("\n");
    printf("  recovery_id : %u   (host computes v: legacy v=chain_id*2+35+rid, EIP-1559 v=rid)\n", sig[64]);
    printf("  sig (r||s||rid, 65 B): 0x");
    print_hex(sig, 65); printf("\n");
}

/* ------------------------- console: sign_digest ------------------------- */

static int cmd_sign_digest(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: sign_digest <key_index> <32-byte hex digest>\n");
        printf("       signs the digest with the key at m/44'/60'/0'/0/<key_index>.\n");
        return 1;
    }
    if (!device_key_provisioned()) { printf("no device key yet — run 'dk_provision' first.\n"); return 1; }

    uint32_t idx = (uint32_t)strtoul(argv[1], NULL, 10);
    uint8_t digest[32];
    int n = hex_to_bytes(argv[2], digest, sizeof(digest));
    if (n != 32) { printf("digest must be exactly 32 bytes of hex (64 hex chars)\n"); return 1; }

    uint8_t sig[65];
    esp_err_t err = wallet_sign_digest(idx, digest, sig);
    device_key_memzero(digest, sizeof(digest));
    if (err != ESP_OK) { printf("sign failed: %s\n", esp_err_to_name(err)); return 1; }

    printf("signed m/44'/60'/0'/0/%lu  digest:\n", (unsigned long)idx);
    print_signature(sig);
    device_key_memzero(sig, sizeof(sig));
    return 0;
}

/* ------------------------- console: sign_eth ---------------------------- */

static int cmd_sign_eth(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: sign_eth <key_index> <unsigned-message-hex>\n");
        printf("       device keccak256's the message, then signs.\n");
        printf("       host pre-builds the message:\n");
        printf("         legacy/EIP-155 : rlp([nonce,gasPrice,gasLimit,to,value,data,chainId,0,0])\n");
        printf("         EIP-1559       : 0x02 || rlp([chainId,nonce,maxPriority,maxFee,gas,to,value,data,accessList])\n");
        printf("         EIP-191        : \"\\x19Ethereum Signed Message:\\n\" + strlen(msg) + msg\n");
        return 1;
    }
    if (!device_key_provisioned()) { printf("no device key yet — run 'dk_provision' first.\n"); return 1; }

    uint32_t idx = (uint32_t)strtoul(argv[1], NULL, 10);
    /* The unsigned message can be up to ~1 KB realistically (tx data is small).
     * Allocate generously. */
    enum { MSG_MAX = 4096 };
    uint8_t *msg = (uint8_t *)malloc(MSG_MAX);
    if (!msg) { printf("oom\n"); return 1; }
    int mlen = hex_to_bytes(argv[2], msg, MSG_MAX);
    if (mlen < 0) { free(msg); printf("bad hex (must be even-length, ≤%u bytes)\n", MSG_MAX); return 1; }

    uint8_t sig[65];
    esp_err_t err = eth_tx_sign(idx, msg, (size_t)mlen, sig);
    device_key_memzero(msg, MSG_MAX);
    free(msg);
    if (err != ESP_OK) { printf("sign failed: %s\n", esp_err_to_name(err)); return 1; }

    printf("signed m/44'/60'/0'/0/%lu  message (%d bytes):\n", (unsigned long)idx, mlen);
    print_signature(sig);
    device_key_memzero(sig, sizeof(sig));
    return 0;
}

/* ------------------------- console: info -------------------------------- */

static int cmd_info(int argc, char **argv)
{
    esp_chip_info_t ci; esp_chip_info(&ci);
    printf("ShrikeVault wallet firmware (P1, DEBUG build)\n");
    printf("  chip      : %s, %d core(s), rev %d.%d\n",
           CONFIG_IDF_TARGET, ci.cores, ci.revision / 100, ci.revision % 100);
    printf("  free heap : %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("  device key: %s\n", device_key_provisioned() ? "provisioned" : "not provisioned (first init)");
    return 0;
}

/* ------------------------- registration --------------------------------- */

static void register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "info",          .help = "Wallet / build info",                                    .func = &cmd_info },
        { .command = "dk_status",     .help = "Show whether the device key is provisioned + fingerprint", .func = &cmd_dk_status },
        { .command = "dk_provision",  .help = "First-init: create the 256-bit device key from HW RNG",  .func = &cmd_dk_provision },
        { .command = "dk_wipe",       .help = "DESTRUCTIVE: erase the device key  (usage: dk_wipe CONFIRM)", .func = &cmd_dk_wipe },
        { .command = "show_mnemonic", .help = "Show the 24-word BIP-39 mnemonic  (usage: show_mnemonic CONFIRM)", .func = &cmd_show_mnemonic },
        { .command = "addr",          .help = "Derive an ETH address at m/44'/60'/0'/0/<index>  (default index=0)", .func = &cmd_addr },
        { .command = "sign_digest",   .help = "Sign a 32-byte digest:  sign_digest <key_idx> <hex>",                .func = &cmd_sign_digest },
        { .command = "sign_eth",      .help = "keccak256(msg) then sign: sign_eth <key_idx> <unsigned-msg-hex>",     .func = &cmd_sign_eth },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

/* ------------------------- app_main ------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "ShrikeVault wallet starting (DEBUG build)");

    ESP_ERROR_CHECK(device_key_init());

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "shrikevault> ";
    repl_cfg.max_cmdline_length = 256;
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw, &repl_cfg, &repl));
    esp_console_register_help_command();
    register_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    ESP_LOGI(TAG, "ready — try 'info', 'dk_status', 'dk_provision'");
}
