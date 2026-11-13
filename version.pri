# Single source of truth for the wfview / wfserver version.
#
# If HEAD has a release tag (for example "v3.0-beta1"), the tag is used.
# Otherwise WFVIEW_BASE_VERSION below is used. Bump it once per release.
# CI or a developer can force a tag with: qmake WFVIEW_TAG_OVERRIDE=v3.0-beta1
#
# Results:
#   WFVIEW_VERSION          full display string, "v3.0-beta1" or "v3.0"
#   WFVIEW_VERSION_NUMERIC  numbers only, "3.0"; used wherever a number is required
#   VERSION                 qmake VERSION (Windows VERSIONINFO, macOS Info.plist), numbers only

WFVIEW_BASE_VERSION = 3.0

isEmpty(WFVIEW_TAG_OVERRIDE) {
    WFVIEW_TAG = $$system(git -C $$shell_quote($$PWD) describe --tags --exact-match HEAD 2> $$QMAKE_SYSTEM_NULL_DEVICE)
} else {
    WFVIEW_TAG = $$WFVIEW_TAG_OVERRIDE
}
WFVIEW_TAG = $$first(WFVIEW_TAG)

WFVIEW_TAG_NUMERIC = $$replace(WFVIEW_TAG, "^v?([0-9]+(\\.[0-9]+){0,3}).*$", "\\1")

isEmpty(WFVIEW_TAG) {
    WFVIEW_VERSION_STRING = v$$WFVIEW_BASE_VERSION
    WFVIEW_VERSION_NUMERIC = $$WFVIEW_BASE_VERSION
} else:contains(WFVIEW_TAG_NUMERIC, "^[0-9]+(\\.[0-9]+){0,3}$") {
    WFVIEW_VERSION_STRING = $$WFVIEW_TAG
    WFVIEW_VERSION_NUMERIC = $$WFVIEW_TAG_NUMERIC
    !equals(WFVIEW_VERSION_NUMERIC, $$WFVIEW_BASE_VERSION) {
        warning("Git tag $$WFVIEW_TAG does not match WFVIEW_BASE_VERSION $$WFVIEW_BASE_VERSION in version.pri")
    }
} else {
    warning("Ignoring git tag $$WFVIEW_TAG: it does not start with a version number")
    WFVIEW_VERSION_STRING = v$$WFVIEW_BASE_VERSION
    WFVIEW_VERSION_NUMERIC = $$WFVIEW_BASE_VERSION
}

VERSION = $$WFVIEW_VERSION_NUMERIC

DEFINES += WFVIEW_VERSION=\\\"$$WFVIEW_VERSION_STRING\\\"
DEFINES += WFVIEW_VERSION_NUMERIC=\\\"$$WFVIEW_VERSION_NUMERIC\\\"

message("wfview version $$WFVIEW_VERSION_STRING (numeric $$WFVIEW_VERSION_NUMERIC)")
