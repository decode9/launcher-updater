/*
 * trust.h - Authenticode check of an executable, for self-update (docs/spec.md, "Self-update").
 *
 * trust_check tells whether a file carries an embedded Authenticode signature, whether Windows
 * accepts it (WinVerifyTrust, generic verify v2 policy, no UI, revocation checked for the whole
 * chain except the root, with CRL/OCSP lookups over the network) and, when signed, the full
 * subject and issuer names of the signer's (leaf) certificate.
 *
 * Revocation that cannot be checked (offline, CRL/OCSP server unreachable) makes the result
 * TRUST_INVALID: the check fails closed. The lookups may take seconds; call it off the UI
 * thread.
 *
 * Wine: WinVerifyTrust is only partly implemented there (no real chain building), so a signed
 * file may come back TRUST_INVALID under Wine; an unsigned file is reported as TRUST_UNSIGNED.
 * Only Windows gives a meaningful TRUST_VALID.
 */
#ifndef LAUNCHER_TRUST_H
#define LAUNCHER_TRUST_H

#include <stddef.h>

typedef enum {
    TRUST_UNSIGNED = 0, /* no embedded signature */
    TRUST_VALID,        /* signed and WinVerifyTrust accepts it */
    TRUST_INVALID       /* signed, but the signature does not verify (or the signer is unreadable) */
} trust_result;

#define TRUST_NAME_BYTES 1024

/* Identity of the signer: the leaf certificate's subject and issuer distinguished names as
 * X.500 strings, most significant RDN first (e.g. "C=US, O=Example, CN=Example Signer"), UTF-8.
 * Both are "" when the file is unsigned or the names cannot be read (or do not fit). */
typedef struct {
    char subject[TRUST_NAME_BYTES];
    char issuer[TRUST_NAME_BYTES];
} trust_signer;

/* path: native UTF-8 path. TRUST_VALID implies both names in signer are non-empty. */
trust_result trust_check(const char *path, trust_signer *signer);

const char *trust_result_text(trust_result r);

#endif
