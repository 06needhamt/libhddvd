# Security policy

libhddvd reads disc data a player gets from anywhere: discs, disc images and
copies of their files. Every parser is meant to treat that data as hostile.
A crash, memory error or hang on crafted input is a security bug.

## Supported versions

Only the latest release, and the `main` branch, get security fixes. Until
1.0 there are no maintenance branches.

## Reporting a vulnerability

Please do not open a public issue or pull request for a vulnerability.
Report it privately through GitHub instead: on the repository's **Security**
tab, choose **Report a vulnerability**. GitHub's documentation calls this
"privately reporting a security vulnerability".

A useful report includes:
- the version or commit;
- how libhddvd was built (compiler, `HDDVD_SCRIPT_ENGINE`, shared or
  static);
- the input that triggers the bug, as small as you can make it;
- the tool or API call and its output, such as an ASan report.

Do not send copyrighted disc content: a reduced or synthetic input is
enough, and `tests/fixtures/generate_objects.py` shows how the project's
own test inputs are built.

libhddvd is maintained by one person, so replies are best effort. You will
get an acknowledgement, and a fix with credit in the release notes unless
you ask not to be named.

## In scope

- Memory errors, undefined behaviour, crashes and infinite loops in any
  parser or tool:
  - ACA archives and object envelopes;
  - XPL, XMF and XMU documents;
  - IFO, VTI and MAP tables;
  - EVO packs;
  - URIs and resource resolution.
- Escapes from the disc root, such as path traversal, encoded separators,
  ambiguous matches, or symlinks on POSIX systems.
- Anything that gets past the limits in `hddvd_config_limits`. That
  includes the script runtime's heap, recursion, operation and time budgets
  and its sandbox: a disc script reaching anything beyond the HDi host
  objects.
- The same issues in the patches under `contrib/`, where they come from
  libhddvd's side.

## Out of scope

- Decryption. libhddvd decrypts nothing and contains no keys. Protected
  content needs a host's provider (`hddvd_decrypt_fn`,
  `hddvd_titles_set_sector_provider`); a provider's own behaviour is the
  host's.
- Bugs in QuickJS itself. Please report them to QuickJS, and tell us if a
  disc script can trigger them through libhddvd so the budgets or sandbox
  can be tightened.
- Outputs that are wrong but safe, such as an unconfirmed layout guess
  reading a structure incorrectly. Please file those as ordinary issues.

## Hardening already in place

CI builds and tests with ASan and UBSan, runs ThreadSanitizer and the Clang
static analyzer, and replays the libFuzzer seed corpus on every push. The
scheduled `fuzz.yml` workflow fuzzes every harness nightly.
