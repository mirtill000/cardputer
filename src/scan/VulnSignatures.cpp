#include "VulnSignatures.h"

namespace {

struct Signature {
    const char* needle;
    const char* note;
};

// Provenance for each entry (why this assistant is confident enough in
// it to hardcode a "vulnerable" verdict without hedging):
//
// - vsftpd 2.3.4: the actual FTP banner string of the compromised
//   tarball distributed for a few weeks in 2011 (CVE-2011-2523) - a
//   deliberately backdoored build, not a version-range guess.
// - ProFTPD 1.3.3c: the exact banner of the source tarball that was
//   replaced with a backdoored copy on proftpd.org's own mirrors in
//   Nov 2010 - same category of incident, different project.
// - OpenSSH 1.x/2.x/3.x: these major versions predate SSH protocol 2
//   hardening and had several real remote holes (e.g. CVE-2002-0640,
//   CVE-2001-0144); anything still identifying as one of these has not
//   been patched in 20+ years by definition.
// - Microsoft-IIS/5.0 and /6.0: Windows 2000 / Server 2003-era IIS,
//   both long past Microsoft's own EOL, both with well-known remote
//   holes (.printer overflow CVE-2001-0241 on IIS5, WebDAV overflow
//   CVE-2017-7269 on IIS6).
// - Apache/1.3 and Apache/2.0: httpd branches with no security patches
//   since 2010 (1.3) / 2013 (2.0.65) respectively.
constexpr Signature kSignatures[] = {
    {"vsftpd 2.3.4", "vsftpd 2.3.4 backdoor (CVE-2011-2523) - compromised source, opens a shell on connect"},
    {"ProFTPD 1.3.3c", "ProFTPD 1.3.3c - backdoored source tarball (Nov 2010 mirror compromise)"},
    {"OpenSSH_1.", "OpenSSH 1.x - pre-SSHv2-hardening, multiple known remote holes, 20+ years unpatched"},
    {"OpenSSH_2.", "OpenSSH 2.x - multiple known remote holes (e.g. CVE-2002-0640), 20+ years unpatched"},
    {"OpenSSH_3.", "OpenSSH 3.x - multiple known remote holes (e.g. CVE-2002-0640), 20+ years unpatched"},
    {"Microsoft-IIS/5.", "IIS 5.0 (Windows 2000-era) - EOL, known remote holes (e.g. CVE-2001-0241)"},
    {"Microsoft-IIS/6.", "IIS 6.0 (Windows Server 2003-era) - EOL, known remote holes (e.g. CVE-2017-7269)"},
    {"Apache/1.3", "Apache httpd 1.3.x - no security patches since 2010"},
    {"Apache/2.0.", "Apache httpd 2.0.x - no security patches since 2013"},
};
constexpr size_t kSignatureCount = sizeof(kSignatures) / sizeof(kSignatures[0]);

}  // namespace

bool VulnSignatures::check(const String& banner, String& noteOut) {
    if (banner.isEmpty()) return false;
    for (size_t i = 0; i < kSignatureCount; i++) {
        if (banner.indexOf(kSignatures[i].needle) >= 0) {
            noteOut = kSignatures[i].note;
            return true;
        }
    }
    return false;
}
