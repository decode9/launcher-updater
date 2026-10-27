/*
 * trust.c - See trust.h. Declarations of wintrust/crypt32 are written by hand (no SDK); only
 * the fields the launcher reads are named.
 */
#include "trust.h"
#include "core/str.h"
#include "fs.h"
#include "rt.h"
#include "win32.h"

#define CERT_QUERY_OBJECT_FILE 1
#define CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED 0x400
#define CERT_QUERY_FORMAT_FLAG_BINARY 2
#define CMSG_SIGNER_INFO_PARAM 6
#define ENCODING (0x00000001UL | 0x00010000UL) /* X509_ASN_ENCODING | PKCS_7_ASN_ENCODING */
#define CERT_FIND_SUBJECT_CERT 0x000B0000UL
#define CERT_NAME_SIMPLE_DISPLAY_TYPE 4
#define WTD_UI_NONE 2
#define WTD_REVOKE_NONE 0
#define WTD_CHOICE_FILE 1
#define WTD_STATEACTION_VERIFY 1
#define WTD_STATEACTION_CLOSE 2
#define WTD_REVOCATION_CHECK_NONE 0x10
#define WTD_CACHE_ONLY_URL_RETRIEVAL 0x1000
#define SIGNER_INFO_BYTES 65536
#define NAME_CHARS 256

typedef struct {
    DWORD data1;
    WORD data2, data3;
    u8 data4[8];
} GUID;

typedef struct {
    DWORD size;
    u8 *data;
} BLOB;

typedef struct {
    char *oid;
    BLOB parameters;
} ALGORITHM_ID;

typedef struct {
    DWORD size;
    u8 *data;
    DWORD unused_bits;
} BIT_BLOB;

typedef struct {
    DWORD low, high;
} FILETIME;

/* CERT_INFO: the search key of CERT_FIND_SUBJECT_CERT (issuer + serial number are used). */
typedef struct {
    DWORD version;
    BLOB serial_number;
    ALGORITHM_ID signature_algorithm;
    BLOB issuer;
    FILETIME not_before, not_after;
    BLOB subject;
    ALGORITHM_ID public_key_algorithm;
    BIT_BLOB public_key;
    BIT_BLOB issuer_unique_id, subject_unique_id;
    DWORD extension_count;
    void *extensions;
} CERT_INFO;

/* Leading fields of CMSG_SIGNER_INFO. */
typedef struct {
    DWORD version;
    BLOB issuer;
    BLOB serial_number;
} SIGNER_INFO_HEAD;

typedef struct {
    DWORD size;
    const wchar_t *path;
    HANDLE file;
    GUID *known_subject;
} WINTRUST_FILE_INFO;

typedef struct {
    DWORD size;
    void *policy_callback_data, *sip_client_data;
    DWORD ui_choice, revocation_checks, union_choice;
    WINTRUST_FILE_INFO *file;
    DWORD state_action;
    HANDLE state_data;
    wchar_t *url_reference;
    DWORD prov_flags, ui_context;
    void *signature_settings;
} WINTRUST_DATA;

IMPORT long WINAPI WinVerifyTrust(HANDLE, GUID *, void *);
IMPORT BOOL WINAPI CryptQueryObject(DWORD, const void *, DWORD, DWORD, DWORD, DWORD *, DWORD *, DWORD *, HANDLE *,
                                    HANDLE *, const void **);
IMPORT BOOL WINAPI CryptMsgGetParam(HANDLE, DWORD, DWORD, void *, DWORD *);
IMPORT BOOL WINAPI CryptMsgClose(HANDLE);
IMPORT BOOL WINAPI CertCloseStore(HANDLE, DWORD);
IMPORT const void *WINAPI CertFindCertificateInStore(HANDLE, DWORD, DWORD, DWORD, const void *, const void *);
IMPORT DWORD WINAPI CertGetNameStringW(const void *, DWORD, DWORD, void *, wchar_t *, DWORD);
IMPORT BOOL WINAPI CertFreeCertificateContext(const void *);

static GUID generic_verify_v2 = {0x00AAC56BUL, 0xCD44, 0x11D0, {0x8C, 0xC2, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE}};
static wchar_t wide_path[FS_PATH_BYTES];
static u8 signer_info[SIGNER_INFO_BYTES];
static wchar_t name[NAME_CHARS];

/* Reads the subject of the first signer's certificate. Returns 1 if the file has an embedded
 * signature (even if the subject cannot be read). */
static int read_signer(char *subject, size_t cap)
{
    DWORD encoding, content, format, size = SIGNER_INFO_BYTES;
    HANDLE store = 0, msg = 0;
    const void *cert = 0;
    CERT_INFO key;
    const SIGNER_INFO_HEAD *head = (const SIGNER_INFO_HEAD *)signer_info;
    int n;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, wide_path, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &content, &format, &store, &msg, 0))
        return 0;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, signer_info, &size)) {
        memset(&key, 0, sizeof key);
        key.issuer = head->issuer;
        key.serial_number = head->serial_number;
        cert = CertFindCertificateInStore(store, ENCODING, 0, CERT_FIND_SUBJECT_CERT, &key, 0);
    }
    if (cert) {
        n = (int)CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, 0, name, NAME_CHARS);
        if (n > 1) {
            n = WideCharToMultiByte(CP_UTF8, 0, name, -1, subject, (int)cap, 0, 0);
            if (n <= 0) subject[0] = 0;
        }
        CertFreeCertificateContext(cert);
    }
    if (msg) CryptMsgClose(msg);
    if (store) CertCloseStore(store, 0);
    return 1;
}

trust_result trust_check(const char *path, char *subject, size_t cap)
{
    WINTRUST_FILE_INFO file;
    WINTRUST_DATA data;
    long verdict;
    if (cap) subject[0] = 0;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, FS_PATH_BYTES) <= 0)
        return TRUST_INVALID;
    if (!read_signer(subject, cap)) return TRUST_UNSIGNED;

    memset(&file, 0, sizeof file);
    file.size = sizeof file;
    file.path = wide_path;
    memset(&data, 0, sizeof data);
    data.size = sizeof data;
    data.ui_choice = WTD_UI_NONE;
    data.revocation_checks = WTD_REVOKE_NONE;
    data.union_choice = WTD_CHOICE_FILE;
    data.file = &file;
    data.state_action = WTD_STATEACTION_VERIFY;
    data.prov_flags = WTD_REVOCATION_CHECK_NONE | WTD_CACHE_ONLY_URL_RETRIEVAL;
    verdict = WinVerifyTrust(INVALID_HANDLE, &generic_verify_v2, &data);
    data.state_action = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE, &generic_verify_v2, &data);
    return verdict == 0 && subject[0] ? TRUST_VALID : TRUST_INVALID;
}

const char *trust_result_text(trust_result r)
{
    switch (r) {
    case TRUST_UNSIGNED: return "unsigned";
    case TRUST_VALID: return "signed (valid)";
    case TRUST_INVALID: break;
    }
    return "signed (not verified)";
}
