using System;
using System.Runtime.InteropServices;
using System.Text;

namespace WebKitTlsSmoke
{
    /// <summary>
    /// Managed view of the native transport probe payload (WebKitTlsProbe.dll).
    /// Owns the P/Invoke surface and the UTF-8 marshaling the payload expects.
    /// </summary>
    internal static class TlsProbeInterop
    {
        /// <summary>Report line written once the managed host is running.</summary>
        internal const string StartupStage = "tls-startup";

        /// <summary>Report line for the package-path resolution outcome.</summary>
        internal const string PackagePathStage = "tls-package-path";

        /// <summary>LocalState report filename shared with the native payload.</summary>
        internal const string ReportFilename = "tls-smoke.txt";

        /// <summary>Packaged CA bundle filename shared with the native payload.</summary>
        internal const string CaBundleFilename = "cacert.pem";

        /// <summary>
        /// Points the native report writer at the host-resolved LocalState path
        /// and truncates any previous report.
        /// </summary>
        /// <param name="logPath">Full LocalState report path.</param>
        [DllImport("WebKitTlsProbe.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode, ExactSpelling = true)]
        internal static extern void WKSetTlsSmokeLogPath(string logPath);

        /// <summary>
        /// Appends one host-owned report line through the native writer so the
        /// line shape, locking and flush behavior match the probe phases.
        /// </summary>
        /// <param name="stage">Stage name (ASCII).</param>
        /// <param name="result">Stage result code.</param>
        internal static void LogResult(string stage, uint result)
        {
            WKLogTlsSmokeResult(ToNullTerminatedUtf8(stage), result);
        }

        /// <summary>
        /// Runs the native crypto/curl phase sequence against the given bundle
        /// path. An empty path flows into the verified phase as an empty CAINFO,
        /// which curl fails closed with no retry or insecure fallback.
        /// </summary>
        /// <param name="caBundleUtf8Path">UTF-8 bundle path, empty when absent.</param>
        internal static void RunProbes(string caBundleUtf8Path)
        {
            WKRunTlsSmokeProbes(ToNullTerminatedUtf8(caBundleUtf8Path ?? string.Empty));
        }

        /// <summary>
        /// Appends one report line in the payload writer (P/Invoke entry).
        /// </summary>
        /// <param name="stage">Null-terminated UTF-8 stage name.</param>
        /// <param name="result">Stage result code.</param>
        [DllImport("WebKitTlsProbe.dll", CallingConvention = CallingConvention.StdCall, ExactSpelling = true)]
        private static extern void WKLogTlsSmokeResult([MarshalAs(UnmanagedType.LPArray)] byte[] stage, uint result);

        /// <summary>
        /// Runs the probe phase sequence (P/Invoke entry).
        /// </summary>
        /// <param name="caBundleUtf8">Null-terminated UTF-8 bundle path.</param>
        [DllImport("WebKitTlsProbe.dll", CallingConvention = CallingConvention.StdCall, ExactSpelling = true)]
        private static extern void WKRunTlsSmokeProbes([MarshalAs(UnmanagedType.LPArray)] byte[] caBundleUtf8);

        /// <summary>
        /// Encodes text as null-terminated UTF-8 for the native payload, whose
        /// bundle path handling is byte-exact rather than system-codepage based.
        /// </summary>
        /// <param name="text">Text to encode.</param>
        /// <returns>Null-terminated UTF-8 bytes.</returns>
        private static byte[] ToNullTerminatedUtf8(string text)
        {
            var body = Encoding.UTF8.GetBytes(text);
            var terminated = new byte[body.Length + 1];
            Buffer.BlockCopy(body, 0, terminated, 0, body.Length);
            terminated[body.Length] = 0;
            return terminated;
        }
    }
}
