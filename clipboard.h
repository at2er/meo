#define CLIPBOARD_READ_BUF sbuf

static const char *clipboard_wlcopycmd[] = {"wl-copy", NULL};
static const char *clipboard_wlpastecmd[] = {"wl-paste", "-n", NULL};

static int
clipboard_get(struct str *s)
{
	const char **cmd = NULL;
	int fds[2], r;

	if (getenv("WAYLAND_DISPLAY"))
		cmd = clipboard_wlpastecmd;
	else
		return 1;


	if (pipe(fds) < 0)
		die("pipe()");

	if (fork() == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0)
			die("dup2()");
		close(fds[1]);
		execvp(cmd[0], (char**)cmd);
		die("execvp()");
	}

	str_empty(s);
	close(fds[1]);
	while ((r = read(fds[0], CLIPBOARD_READ_BUF, sizeof(CLIPBOARD_READ_BUF) - 1)))
		estr_append_str(s, &STR(CLIPBOARD_READ_BUF, r));
	close(fds[0]);

	if (!s->s)
		return 1;
	return 0;
}

static int
clipboard_set(const struct str *s)
{
	const char **cmd = NULL;
	int fds[2];

	if (getenv("WAYLAND_DISPLAY"))
		cmd = clipboard_wlcopycmd;
	else
		return 1;

	if (pipe(fds) < 0)
		die("pipe()");
	if (fork() == 0) {
		close(fds[1]);
		if (dup2(fds[0], STDIN_FILENO) < 0)
			die("dup2()");
		close(fds[0]);
		execvp(cmd[0], (char**)cmd);
		die("execvp()");
	}

	close(fds[0]);
	write(fds[1], s->s, s->len);
	close(fds[1]);

	return 0;
}
