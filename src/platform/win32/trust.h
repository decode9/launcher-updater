/*
 * trust.h - Authenticode check of an executable, for self-update (docs/spec.md, "Self-update").
 *
 * trust_check tells whether a file carries an embedded Authenticode signature, whether Windows
 * accepts it (WinVerifyTrust, generic verify v2 policy, no UI, no revocation lookups over the
 * network) and, when signed, the display name of the signer certificate's subject.
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

/* path: native UTF-8 path. subject receives the signer's subject name ("" if unsigned or
 * unreadable). */
trust_result trust_check(const char *path, char *subject, size_t cap);

const char *trust_result_text(trust_result r);

#endif
