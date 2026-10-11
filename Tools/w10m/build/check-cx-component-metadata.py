"""Inspect compiler-derived IDL for the M10 revision, never prototype metadata."""

import argparse
from pathlib import Path
import re


def check(idl: Path, api: Path) -> None:
    """Verify compiler-derived public shape, hidden capabilities and authored placement."""
    text = idl.read_text(encoding="utf-8-sig")
    prototype_iids = {
        "da4dd758-dbf5-4568-8074-d92300fdb7b8",
        "df4d11eb-659d-478d-a21e-f3a1b1ea0903",
        "1e12f19a-bd8e-4bf9-be0c-2f4de1534766",
        "0d6aeba7-2b48-4508-9751-6b56cf1d64dc",
        "331252db-3b4c-4011-b830-b7a34cc5b68c",
        "e2aaa8e0-f399-4a52-bc39-3f55669bec24",
        "7813f950-7875-4d1f-841d-b71419cc3865",
    }
    assert not any(iid in text.lower() for iid in prototype_iids), "Incompatible prototype IID reused"
    assert re.search(r"runtimeclass WebKitWebView\s*:\s*Windows.UI.Xaml.Controls.UserControl", text), "Wrong XAML base"
    assert "[default] interface WebKit.IWebKitWebViewV2;" in text, "Wrong revision-2 default interface"
    assert "[uuid(B0CACD92-A344-4B7B-9E94-99C3C3BFB35C)]" in text, "Revision-2 IID drift"

    def interface_body(name: str) -> str:
        """Extract one generated default/argument interface's method declarations."""
        body = re.search(rf"interface {name} : IInspectable\s*\{{([^}}]*)\}}", text)
        assert body, f"Missing interface: {name}"
        return body[1]

    def properties(name: str, fields: dict[str, str], writable: set[str]) -> None:
        """Verify exact getter signatures and the complete allowed setter set."""
        body = interface_body(name)
        getters = re.findall(r"\[propget\] HRESULT (\w+)\(\[out\] \[retval\] ([^;]+) __returnValue\);", body)
        assert dict(getters) == fields, f"Property signature drift: {name}"
        assert set(re.findall(r"\[propput\] HRESULT (\w+)\(", body)) == writable, f"Unexpected mutability: {name}"

    properties("IWebKitWebViewV2", {"Source": "Windows.Foundation.Uri**", "CanGoBack": "boolean*",
               "CanGoForward": "boolean*", "IsLoading": "boolean*", "DocumentTitle": "HSTRING*"}, {"Source"})
    control = interface_body("IWebKitWebViewV2")
    assert "[propput] HRESULT Source([in] Windows.Foundation.Uri* value);" in control
    assert "HRESULT EnsureInitializedAsync([out] [retval] Windows.Foundation.IAsyncAction** __returnValue);" in control
    for method in ("GoBack", "GoForward", "Reload", "Stop", "Close"):
        assert f"HRESULT {method}();" in control, f"Missing method: {method}"
    assert "HRESULT ProceedWithCertificateFailure([in] WebKit.WebKitCertificateFailureToken* token);" in control
    assert re.search(r"HRESULT NavigateToString\d+\(\[in\] HSTRING html\);", control)
    assert re.search(r"HRESULT NavigateToString\d+\(\[in\] HSTRING html, \[in\] WebKit.WebKitCertificateFailureToken\* warningToken\);", control)
    for event, args in {"NavigationStarting": "WebKitNavigationStartingEventArgs", "SourceChanged": "WebKitSourceChangedEventArgs",
                        "HistoryChanged": "WebKitHistoryChangedEventArgs", "DocumentTitleChanged": "WebKitDocumentTitleChangedEventArgs",
                        "NavigationCompleted": "WebKitNavigationCompletedEventArgs"}.items():
        assert f"[eventadd] HRESULT {event}([in] Windows.Foundation.TypedEventHandler<WebKit.WebKitWebView*, WebKit.{args}*>* value" in control
        assert f"[eventremove] HRESULT {event}([in] EventRegistrationToken value);" in control
    assert "interface Windows.UI.Xaml.Data.INotifyPropertyChanged;" in text
    assert "interface Windows.UI.Xaml.Data.ICustomPropertyProvider;" in text
    properties("__IWebKitCertificateFailureTokenPublicNonVirtuals", {"FailedNavigationId": "UINT64*", "FailedUri": "Windows.Foundation.Uri**"}, set())
    properties("__IWebKitNavigationStartingEventArgsPublicNonVirtuals", {"NavigationId": "UINT64*", "Uri": "Windows.Foundation.Uri**",
               "ContentKind": "WebKit.WebKitNavigationContentKind*", "Cancel": "boolean*"}, {"Cancel"})
    properties("__IWebKitNavigationCompletedEventArgsPublicNonVirtuals", {"NavigationId": "UINT64*", "Uri": "Windows.Foundation.Uri**",
               "ContentKind": "WebKit.WebKitNavigationContentKind*", "Outcome": "WebKit.WebKitNavigationOutcome*",
               "ErrorDomain": "WebKit.WebKitNavigationErrorDomain*", "ErrorCode": "INT32*", "HasHttpStatus": "boolean*", "HttpStatus": "UINT32*",
               "CertificateFailureToken": "WebKit.WebKitCertificateFailureToken**", "CanProceedWithCertificateFailure": "boolean*"}, set())
    properties("__IWebKitSourceChangedEventArgsPublicNonVirtuals", {"Uri": "Windows.Foundation.Uri**", "IsNewDocument": "boolean*",
               "ChangeKind": "WebKit.WebKitSourceChangeKind*"}, set())
    properties("__IWebKitHistoryChangedEventArgsPublicNonVirtuals", {"CanGoBack": "boolean*", "CanGoForward": "boolean*"}, set())
    properties("__IWebKitDocumentTitleChangedEventArgsPublicNonVirtuals", {"DocumentTitle": "HSTRING*"}, set())
    properties("__IWebKitWebViewOptionsPublicNonVirtuals", {"CertificateAuthorityPath": "HSTRING*", "FontDirectoryPath": "HSTRING*",
               "MaximumConcurrentRequests": "UINT32*", "MaximumResourceBytes": "UINT64*", "MaximumBufferedBytes": "UINT64*",
               "RequestTimeoutMilliseconds": "UINT32*"}, {"CertificateAuthorityPath", "FontDirectoryPath", "MaximumConcurrentRequests",
               "MaximumResourceBytes", "MaximumBufferedBytes", "RequestTimeoutMilliseconds"})
    assert "HRESULT CreateInstance1([in] WebKit.WebKitWebViewOptions* options, [out] [retval] WebKit.WebKitWebView** __returnValue);" in text
    assert "[propget] HRESULT SourceProperty([out] [retval] Windows.UI.Xaml.DependencyProperty** __returnValue);" in text
    classes = (
        "WebKitWebView", "WebKitWebViewOptions", "WebKitCertificateFailureToken",
        "WebKitNavigationStartingEventArgs", "WebKitNavigationCompletedEventArgs",
        "WebKitSourceChangedEventArgs", "WebKitHistoryChangedEventArgs", "WebKitDocumentTitleChangedEventArgs",
    )
    for name in classes:
        assert re.search(rf"runtimeclass {name}(?:\s*:[^{{]+)?\s*\{{", text), f"Missing runtime class: {name}"
    for enum, values in {
        "WebKitNavigationOutcome": {"Completed": 0, "Failed": 1, "Cancelled": 2},
        "WebKitNavigationErrorDomain": {"None": 0, "Transport": 1, "Network": 2, "Engine": 3, "Platform": 4},
        "WebKitSourceChangeKind": {"NewDocument": 0, "Fragment": 1, "PushState": 2, "ReplaceState": 3, "HistoryTraversal": 4},
        "WebKitNavigationContentKind": {"Remote": 0, "HostHtml": 1},
    }.items():
        body = re.search(rf"enum {enum}\s*\{{([^}}]+)\}}", text)
        assert body, f"Missing enum: {enum}"
        actual = {name: int(value) for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", body[1])}
        assert actual == values, f"Wrong enum values: {enum}"
    # Nonconstructibility is a metadata fact: no activatable/factory declaration
    # may be attached to the token or copied event-argument classes.
    for name in classes[2:]:
        declaration = re.search(rf"((?:\s*\[[^\]]+\])+\s*)runtimeclass {name}\b", text)
        assert declaration and "activatable" not in declaration[1].lower(), f"Public activation exposed: {name}"
        assert not re.search(rf"interface __I{name}(?:Factory|Statics)\b", text), f"Capability factory/statics exposed: {name}"
    for basename in ("WebKitWebView", "WebKitWebViewOptions", "WebKitNavigationEventArgs", "WebKitCertificateFailureToken"):
        for extension in ("h", "cpp"):
            assert (api / f"{basename}.{extension}").is_file(), f"Wrong authored filename: {basename}.{extension}"
    assert (api / "WebKitNavigationTypes.h").is_file()
    for hidden in ("EngineToken", "IssuedBy", "EndDispatch", "WKW10MCertificateToken", "controlIdentity", "CopyToNative", "PrepareConfiguration",
                   "SetCommandHandler", "CompleteInitialization", "HandleStarting", "HandleNotification", "ControlPhase", "HandlerFailures",
                   "BuildPageCallbacks", "LastPageCallbackResult", "IsCurrentCertificateToken", "InvalidateCertificateToken",
                   "BindPageController", "ExecutePageCommand", "QueuePageCommand", "QueueAfterInitialization",
                   "PageCallbacksReleased", "FinishClose", "RevokePublicEvents", "DeferredCommandFailures", "LastDeferredCommandError", "AdvanceDocumentIntent", "ExecuteInitialSource"):
        assert hidden not in text, f"Internal member exposed: {hidden}"
    print("M10 metadata PASS: XAML base, eight classes, four enums, hidden activation, filenames, revision and new IIDs")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idl", required=True, type=Path)
    parser.add_argument("--api", required=True, type=Path)
    args = parser.parse_args()
    check(args.idl, args.api)
