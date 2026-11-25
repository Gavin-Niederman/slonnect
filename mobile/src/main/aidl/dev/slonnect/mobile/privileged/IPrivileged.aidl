// Runs in a Shizuku user-service process as the adb shell user (or root), see PrivilegedService.
package dev.slonnect.mobile.privileged;

interface IPrivileged {
    // Shizuku calls this to stop the service.
    void destroy() = 16777114;

    // "exitCode\nstdout+stderr" of `sh -c command`, at most timeoutMs.
    String exec(String command, long timeoutMs) = 1;

    // mode: "connect" | "disconnect". Returns "" on success, otherwise an error message.
    String bluetooth(String address, String mode) = 2;
}
