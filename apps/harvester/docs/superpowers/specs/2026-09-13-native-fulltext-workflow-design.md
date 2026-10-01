# Native Full-Text Workflow Design

## Status

Accepted and implemented on 2026-09-13. This design extends the native search
workflow with DOI/OA metadata, explicit open-access candidate discovery, and
explicit local PDF download. It does not add support for sources known to
distribute unauthorized copies.

## Goal and scope

The native application must let a researcher do three things after selecting a
search result:

1. Retain the result's DOI and any source-provided open-access URL.
2. Discover candidate full-text locations without changing local files or the
   PostgreSQL library.
3. Download exactly one candidate only after the user explicitly selects it.

The implementation is local-only, but it still treats source metadata, remote
responses, redirects, and PDF bytes as untrusted. No automatic background
download, database write, browser launch, credential forwarding, or arbitrary
URL entry is in scope.

## Architecture

```text
screen -> ResearchService -> PythonResearchService -> desktop worker
    |             |                     |                 |
    |             |                     |                 +-- source metadata APIs
    |             |                     +-- owns opaque candidate IDs
    |             +-- typed snapshots only
    +-- never receives a remote URL or filesystem path for mutation
```

`WorkSummary` gains optional DOI and source-provided open-access URL fields.
The application-service contract gains two non-blocking operations:

- `begin_fulltext_discovery(work)` / `poll_fulltext_discovery(ticket)`
- `begin_fulltext_download(candidate_id)` / `poll_fulltext_download(ticket)`

`FullTextCandidate` exposes an opaque candidate ID, source label, repository,
license/version metadata, host label, and whether a direct PDF URL was found.
The Python service owns the actual candidate URL in a bounded cache; neither
ImGui nor the screen passes an editable URL into a download operation.

`PythonResearchService` retains its process, output-size, timeout, and cleanup
rules. It adds operation-specific worker arguments through the existing fixed
`execvp` prefix; no request uses a shell. A worker protocol v2 will carry the
extra search fields and typed full-text discovery/download responses. The v1
search parser remains accepted during the transition so old worker output fails
closed only when it attempts an unsupported operation.

## Discovery and download behavior

The implemented provider set uses source-provided OA locations, OpenAlex,
OpenAIRE Graph v3, Europe PMC, and Zenodo. It accepts only public,
unauthenticated metadata queries; OpenAIRE instances and Europe PMC results
must be explicitly open access, while Zenodo records must be public/open and
match the selected work's DOI exactly. Its module boundary accepts additional
public metadata/repository providers without importing the legacy
configuration, database, or CLI layer. A source failure is a source-name-only
notice; raw provider diagnostics are not displayed. Candidates are normalized,
deduplicated, and capped at 200 before the worker protocol, whose output is
also defensively capped at the same bound.

Candidate URLs must be HTTPS, parse cleanly, have a public DNS result, and
never resolve to loopback, link-local, private, multicast, or otherwise
reserved addresses. IP literals are rejected; each download TCP connection is
pinned to a validated public address while preserving TLS hostname
verification. Redirects are followed manually only after the same checks for
each hop. The worker never accepts an arbitrary URL typed by the user.

The candidate table is read-only. Selecting **Download selected PDF** starts a
fresh worker operation for that opaque candidate. The worker streams at most
64 MiB into a temporary file, requires a PDF magic header, hashes its contents,
and atomically renames it into the application-owned XDG data directory:

```text
$XDG_DATA_HOME/research-harvester/papers/
~/.local/share/research-harvester/papers/  # fallback
```

Filename components are generated from a normalized title and checksum; no
remote filename or user-controlled path selects the destination. The UI shows
only the successful local filename, byte count, SHA-256, source, and license.
It does not write a `documents` database record in this increment.

## UI behavior

The selected-work panel replaces the placeholder full-text action with
`Find full text`. It displays a pending state without blocking rendering, then
a compact candidate table. A candidate must be selected before the download
button enables. Download state is shown next to the selected work; success
offers the local filename and metadata, while failure uses a bounded,
user-facing error.

`Queue enrichment` remains visibly unavailable as a later service operation;
it is not silently treated as complete.

## Testing and verification

- Python unit tests: source response mapping for OpenAlex, OpenAIRE, Europe
  PMC, and both supported Zenodo record shapes; candidate deduplication and
  bounds; private address/redirect rejection; response size/PDF-header
  rejection; safe filename generation; atomic output behavior; and no-database
  discovery.
- C++ protocol tests: v1 compatibility, v2 work metadata, candidate records,
  unknown/stale candidate IDs, malformed output, and bounded strings.
- C++ process-adapter fixture: pending → candidates → explicit download,
  worker error, oversized output, child cleanup, and invalid candidate ID.
- UI/manual: discover from a live DOI-bearing result, select a candidate,
  confirm no file exists before clicking download, download one valid PDF, and
  verify resize/input still work.
- Regression: full native CTest, Python unittest suite, hardware OpenGL and
  llvmpipe smokes, plus Valgrind for the worker adapter.

## Deferred work

Persistent library records, PDF import from arbitrary local paths, enrichment,
related graph, content extraction, user-configurable source policy, Unpaywall,
CORE, DOI landing-page crawling, and Vulkan remain separate increments.
Support for sources known to distribute unauthorized copies is out of scope.
