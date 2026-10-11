// Exercise process-group setup without relying on the macOS scheduling race.
// The real runtime still forks, executes, captures output and reaps its child.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <errno.h>
#include <unistd.h>

static int controlled_setpgid(pid_t, pid_t);
#define setpgid controlled_setpgid
#include "../runtime/beans_rt.c"
#undef setpgid

long long beans_class_parents[1] = {-1};
long long beans_deinit_sel = -1;

static int group_established;
static int controlled_setpgid(pid_t pid, pid_t pgid) {
    // Suppress the parent's racing call so both child outcomes are controlled.
    if (pid != 0) return 0;
    if (group_established) assert(setpgid(pid, pgid) == 0);
    errno = EPERM;
    return -1;
}

int main(void) {
    const char command[] = "/bin/echo\0started\0";
    BList* argv = bytes_mk(sizeof command - 1);
    memcpy(argv->data, command, sizeof command - 1);
    beans_proc_timeout_scope(30000);
    for (group_established = 0; group_established < 2; group_established++) {
        BRes result = beans_proc_run(argv, NULL, NULL, NULL, 1024);
        if (group_established) {
            assert(result.err == NULL);
            BList* parts = (BList*)(intptr_t)result.val;
            BList** items = (BList**)parts->data;
            assert(parts->len == 3);
            assert(items[0]->len == 8 && rt_load_le(items[0]->data, 8) == 0);
            assert(items[1]->len == 8 && memcmp(items[1]->data, "started\n", 8) == 0);
            assert(items[2]->len == 0);
            beans_release(parts);
        } else {
            assert(result.val == 0 && result.err != NULL);
            assert(strcmp(((BError*)result.err)->kind, "permission") == 0);
            beans_release(result.err);
        }
        // Both the rejected launch and the successful launch must be reaped.
        int status;
        errno = 0;
        assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    }
    beans_release(argv);
    puts("ok process groups: established group accepted, setup failure rejected");
    return 0;
}
