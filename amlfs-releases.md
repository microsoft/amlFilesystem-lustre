
# Monitoring AMLFS Releases

To stay informed about new official releases of the GitHub repository
<https://github.com/microsoft/amlFilesystem-lustre>, users can subscribe or
monitor updates through several methods. Since this repository includes upstream
tags that are not necessarily AMLFS-related releases, it is crucial to focus
only on GitHub "Releases," which will be marked with an `AMLFS_` prefix.

## Methods for Monitoring Releases

### 1. GitHub Web Interface Subscription

Users can manually subscribe to the repository's releases by navigating to the
repository page, clicking the "Watch" button near the top right, and selecting
"Custom." From there, choose to be notified only about releases. This method
sends notifications via email or GitHub notifications whenever a new release
with the `AMLFS_` prefix is published.

**Benefits:**

- Easy to set up, no technical knowledge required
- Integrated with GitHub notifications system
- Direct email notifications to your registered email

**Downsides:**

- Notifications may be delayed or missed if email filters interfere
- Requires a GitHub account
- Limited customization of notification format

### 2. GitHub CLI (`gh`)

The GitHub CLI tool allows users to list and monitor releases programmatically.
Running `gh release list --repo microsoft/amlFilesystem-lustre` will show
available releases. Users can script periodic checks to detect new releases
with the `AMLFS_` prefix. To find the commit referenced by a release, use
`gh release view <release-tag> --repo microsoft/amlFilesystem-lustre` which
displays release details including the target commit.

**Benefits:**

- Automatable and scriptable
- Can be integrated into CI/CD pipelines
- Provides detailed release information including commit references
- Works offline once data is fetched

**Downsides:**

- Requires installation and familiarity with command line tools
- Need to set up authentication tokens for private repositories
- Manual scripting required for automated monitoring

### 3. GitHub REST API

Developers can query the GitHub REST API endpoint for releases:
`https://api.github.com/repos/microsoft/amlFilesystem-lustre/releases`. This
returns JSON data including release tags, release notes, and the commit SHA
each release points to. Filtering for tags starting with `AMLFS_` ensures only
official releases are tracked. This method supports full automation and
integration into custom monitoring tools.

**Benefits:**

- Highly customizable and flexible
- Real-time data access with proper polling
- Supports integration with various platforms and tools

**Downsides:**

- Need to handle API rate limits (5000 requests/hour for authenticated users)
- Requires programming knowledge, proper error handling, and retry logic if
  automated

### 4. GitHub Atom/RSS Feeds

GitHub provides Atom feeds for releases that can be consumed by RSS readers or
monitoring tools. The feed URL follows the pattern:
`https://github.com/microsoft/amlFilesystem-lustre/releases.atom`. Most RSS
readers and feed aggregators can monitor this URL for new entries.

**Benefits:**

- Simple to subscribe through common feed readers
- No GitHub account required for public repositories
- Standardized format supported by many tools
- Low resource overhead

**Downsides:**

- Limited filtering capabilities (cannot filter by `AMLFS_` prefix natively)
- May include all releases, requiring manual filtering
- Potential delays depending on feed reader refresh intervals

## Recommendations

**For Casual Users:**

- Use GitHub's web interface subscription for simplicity and direct
  integration

**For Developers and Teams:**

- Use GitHub CLI (`gh`) commands for scripted monitoring and automation
- Implement GitHub API integration for custom workflows and advanced
  filtering
- Use API-based solutions for dashboard integration and metrics collection

## Important Notes

- Always verify that the release tag starts with `AMLFS_` to avoid confusion
  with non-release tags
- Release details include the exact commit reference, enabling users to track
  the code state associated with each release
- Consider implementing multiple monitoring methods for redundancy in critical
  environments
- API rate limits apply to GitHub API usage - implement appropriate caching
  and throttling
- For automated solutions, implement proper error handling and retry logic to
  handle temporary service unavailability
