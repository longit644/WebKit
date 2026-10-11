using System;
using System.Runtime.InteropServices;

namespace WebKitFpuProbe
{
    /// <summary>
    /// P/Invoke boundary to the native FPU/ARM payload. The payload DLL keeps
    /// its existing native style and exports; this class is the only contract
    /// between the C# host and native code.
    /// </summary>
    internal static class NativeMethods
    {
        /// <summary>Name of the native payload module staged in the package.</summary>
        private const string PayloadModule = "FpuProbePayload.dll";

        /// <summary>
        /// Runs the full native probe sequence on the calling thread and
        /// appends every report line to LocalState\fpuprobe.log.
        /// </summary>
        /// <returns>
        /// Zero when the sequence completes; one when the inbox CRT symbols
        /// are missing. Must be called on the owning UI thread.
        /// </returns>
        [DllImport(PayloadModule, EntryPoint = "WKFpuProbeRun", CallingConvention = CallingConvention.StdCall, ExactSpelling = true)]
        internal static extern int RunProbe();
    }
}
