#include "tmuxremote_service.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __APPLE__

#include <mach-o/dyld.h>
#include <limits.h>

#define LAUNCHAGENT_LABEL "com.tmuxremote.agent"
#define PLIST_FILENAME LAUNCHAGENT_LABEL ".plist"

static bool get_plist_path(char* buf, size_t bufsz)
{
    const char* home = getenv("HOME");
    if (home == NULL) {
        printf("Cannot determine HOME directory\n");
        return false;
    }
    snprintf(buf, bufsz, "%s/Library/LaunchAgents/%s", home, PLIST_FILENAME);
    return true;
}

static bool get_executable_path(char* resolved, size_t resolvedsz)
{
    char rawPath[PATH_MAX];
    uint32_t size = sizeof(rawPath);
    if (_NSGetExecutablePath(rawPath, &size) != 0) {
        printf("Cannot determine executable path\n");
        return false;
    }
    char* rp = realpath(rawPath, NULL);
    if (rp == NULL) {
        printf("Cannot resolve executable path\n");
        return false;
    }
    snprintf(resolved, resolvedsz, "%s", rp);
    free(rp);
    return true;
}

static void ensure_launchagents_dir(void)
{
    const char* home = getenv("HOME");
    if (home == NULL) {
        return;
    }
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/Library/LaunchAgents", home);
    mkdir(dir, 0755);
}

bool tmuxremote_do_install_service(const char* homeDir)
{
    char plistPath[PATH_MAX];
    if (!get_plist_path(plistPath, sizeof(plistPath))) {
        return false;
    }

    char exePath[PATH_MAX];
    if (!get_executable_path(exePath, sizeof(exePath))) {
        return false;
    }

    char logPath[PATH_MAX];
    snprintf(logPath, sizeof(logPath), "%s/agent.log", homeDir);

    ensure_launchagents_dir();

    /* If plist already exists, unload it first (ignore errors) */
    if (access(plistPath, F_OK) == 0) {
        char cmd[PATH_MAX + 64];
        snprintf(cmd, sizeof(cmd), "launchctl unload '%s' 2>/dev/null", plistPath);
        system(cmd);
    }

    FILE* f = fopen(plistPath, "w");
    if (f == NULL) {
        printf("Cannot write plist: %s\n", plistPath);
        return false;
    }

    fprintf(f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\"\n"
        "  \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>Label</key>\n"
        "  <string>%s</string>\n"
        "  <key>ProgramArguments</key>\n"
        "  <array>\n"
        "    <string>%s</string>\n"
        "  </array>\n"
        "  <key>RunAtLoad</key>\n"
        "  <true/>\n"
        "  <key>KeepAlive</key>\n"
        "  <dict>\n"
        "    <key>Crashed</key>\n"
        "    <true/>\n"
        "  </dict>\n"
        "  <key>StandardOutPath</key>\n"
        "  <string>%s</string>\n"
        "  <key>StandardErrorPath</key>\n"
        "  <string>%s</string>\n"
        "  <key>EnvironmentVariables</key>\n"
        "  <dict>\n"
        "    <key>PATH</key>\n"
        "    <string>/usr/local/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>\n"
        "  </dict>\n"
        "</dict>\n"
        "</plist>\n",
        LAUNCHAGENT_LABEL, exePath, logPath, logPath);

    fclose(f);
    chmod(plistPath, 0644);

    char cmd[PATH_MAX + 64];
    snprintf(cmd, sizeof(cmd), "launchctl load '%s'", plistPath);
    int rc = system(cmd);
    if (rc != 0) {
        printf("Warning: launchctl load returned %d\n", rc);
    }

    printf("LaunchAgent installed and loaded.\n");
    printf("  Plist:  %s\n", plistPath);
    printf("  Binary: %s\n", exePath);
    printf("  Log:    %s\n", logPath);
    printf("\n");
    printf("The agent will start automatically on login.\n");
    printf("To stop:   launchctl stop %s\n", LAUNCHAGENT_LABEL);
    printf("To start:  launchctl start %s\n", LAUNCHAGENT_LABEL);
    printf("To remove: tmux-remote-agent --uninstall-service\n");
    return true;
}

bool tmuxremote_do_uninstall_service(void)
{
    char plistPath[PATH_MAX];
    if (!get_plist_path(plistPath, sizeof(plistPath))) {
        return false;
    }

    if (access(plistPath, F_OK) != 0) {
        printf("LaunchAgent is not installed.\n");
        return true;
    }

    char cmd[PATH_MAX + 64];
    snprintf(cmd, sizeof(cmd), "launchctl unload '%s' 2>/dev/null", plistPath);
    system(cmd);

    if (unlink(plistPath) != 0) {
        printf("Failed to remove plist: %s\n", plistPath);
        return false;
    }

    printf("LaunchAgent uninstalled.\n");
    printf("  Removed: %s\n", plistPath);
    return true;
}

bool tmuxremote_do_service_status(void)
{
    char plistPath[PATH_MAX];
    if (!get_plist_path(plistPath, sizeof(plistPath))) {
        return false;
    }

    if (access(plistPath, F_OK) != 0) {
        printf("LaunchAgent is not installed.\n");
        return true;
    }

    printf("LaunchAgent is installed.\n");
    printf("  Plist: %s\n", plistPath);

    /* Use launchctl list to check if loaded and get PID */
    FILE* p = popen("launchctl list " LAUNCHAGENT_LABEL " 2>/dev/null", "r");
    if (p == NULL) {
        printf("  Status: unknown (cannot run launchctl)\n");
        return true;
    }

    char line[256];
    int pid = -1;
    int lastExitStatus = -1;
    bool found = false;

    while (fgets(line, sizeof(line), p) != NULL) {
        /* Look for PID line: "\"PID\" = 12345;" */
        int val;
        if (sscanf(line, " \"PID\" = %d;", &val) == 1) {
            pid = val;
            found = true;
        }
        if (sscanf(line, " \"LastExitStatus\" = %d;", &val) == 1) {
            lastExitStatus = val;
            found = true;
        }
    }
    pclose(p);

    if (!found) {
        printf("  Status: not loaded (launchctl has no record)\n");
        return true;
    }

    if (pid > 0) {
        printf("  Status: running (PID %d)\n", pid);
    } else {
        printf("  Status: stopped");
        if (lastExitStatus >= 0) {
            printf(" (last exit status: %d)", lastExitStatus);
        }
        printf("\n");
    }

    return true;
}

#else /* !__APPLE__ */

bool tmuxremote_do_install_service(const char* homeDir)
{
    (void)homeDir;
    printf("LaunchAgent is only supported on macOS.\n");
    return false;
}

bool tmuxremote_do_uninstall_service(void)
{
    printf("LaunchAgent is only supported on macOS.\n");
    return false;
}

bool tmuxremote_do_service_status(void)
{
    printf("LaunchAgent is only supported on macOS.\n");
    return false;
}

#endif
