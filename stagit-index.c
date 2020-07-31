#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <git2.h>

static git_repository *repo;

static const char *relpath = "";

static char description[255] = "Oscar Benedito's Git repositories";
static char *name = "";
static char owner[255];

void
joinpath(char *buf, size_t bufsiz, const char *path, const char *path2)
{
	int r;

	r = snprintf(buf, bufsiz, "%s%s%s",
		path, path[0] && path[strlen(path) - 1] != '/' ? "/" : "", path2);
	if (r < 0 || (size_t)r >= bufsiz)
		errx(1, "path truncated: '%s%s%s'",
			path, path[0] && path[strlen(path) - 1] != '/' ? "/" : "", path2);
}

/* Escape characters below as HTML 2.0 / XML 1.0. */
void
xmlencode(FILE *fp, const char *s, size_t len)
{
	size_t i;

	for (i = 0; *s && i < len; s++, i++) {
		switch(*s) {
		case '<':  fputs("&lt;",   fp); break;
		case '>':  fputs("&gt;",   fp); break;
		case '\'': fputs("&#39;" , fp); break;
		case '&':  fputs("&amp;",  fp); break;
		case '"':  fputs("&quot;", fp); break;
		default:   fputc(*s, fp);
		}
	}
}

void
printtimeshort(FILE *fp, const git_time *intime)
{
	struct tm *intm;
	time_t t;
	char out[32];

	t = (time_t)intime->time;
	if (!(intm = gmtime(&t)))
		return;
	strftime(out, sizeof(out), "%Y-%m-%d %H:%M", intm);
	fputs(out, fp);
}

void
writeheader(FILE *fp)
{
	fputs("<!DOCTYPE html>\n"
		"<html>\n<head>\n"
		"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" />\n"
		"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
		"<title>Git Repositories | Oscar Benedito</title>\n", fp);
	fprintf(fp, "<link rel=\"icon\" type=\"image/png\" href=\"%sfavicon.ico\" />\n", relpath);
	fprintf(fp, "<link rel=\"stylesheet\" type=\"text/css\" href=\"%sstyle.css\" />\n", relpath);
	fputs("</head>\n<body id=\"home\">\n<h1>", fp);
	xmlencode(fp, description, strlen(description));
	fputs("</h1>\n<div id=\"content\">\n"
		"<h2 id=\"repositories\">Repositories</h2>\n"
		"<div class=\"table-container\">\n<table id=\"index\"><thead>\n"
		"<tr><td><b>Name</b></td><td><b>Description</b></td><td><b>Last commit</b></td></tr>"
		"</thead><tbody>\n", fp);
}

void
writefooter(FILE *fp)
{
	fputs("</tbody>\n</table>\n</div>\n"
		"<h2 id=\"contribute\">Contribute</h2>\n"
		"<p>The best way to contribute to my repositories is through e-mail, check out <a href=\"https://git-send-email.io\">git-send-email.io</a> if you don’t know how to do that. Send your patches to <a href=\"mailto:patches@oscarbenedito.com\">patches@oscarbenedito.com</a> and change the subject prefix to specify the repository you are sending the patch for. You can do that running the following command from the git repository:</p>\n"
		"<pre><code>git config format.subjectPrefix \"PATCH &lt;name-of-repository&gt;\"</code></pre>\n"
		"<p>You can also contribute on <a href=\"https://gitlab.com/oscarbenedito\">GitLab</a> or <a href=\"https://github.com/oscarbenedito\">GitHub</a> (all my public repositories should be on both platforms) doing pull requests.</p>\n"
		"</div>\n</body>\n</html>\n", fp);
}

int
writelog(FILE *fp)
{
	git_commit *commit = NULL;
	const git_signature *author;
	git_revwalk *w = NULL;
	git_oid id;
	char *stripped_name = NULL, *p;
	int ret = 0;

	git_revwalk_new(&w, repo);
	git_revwalk_push_head(w);
	git_revwalk_simplify_first_parent(w);

	if (git_revwalk_next(&id, w) ||
	    git_commit_lookup(&commit, repo, &id)) {
		ret = -1;
		goto err;
	}

	author = git_commit_author(commit);

	/* strip .git suffix */
	if (!(stripped_name = strdup(name)))
		err(1, "strdup");
	if ((p = strrchr(stripped_name, '.')))
		if (!strcmp(p, ".git"))
			*p = '\0';

	fputs("<tr><td><a href=\"", fp);
	xmlencode(fp, stripped_name, strlen(stripped_name));
	fputs("/\">", fp);
	xmlencode(fp, stripped_name, strlen(stripped_name));
	fputs("</a></td><td>", fp);
	xmlencode(fp, description, strlen(description));
	fputs("</td><td>", fp);
	if (author)
		printtimeshort(fp, &(author->when));
	fputs("</td></tr>", fp);

	git_commit_free(commit);
err:
	git_revwalk_free(w);
	free(stripped_name);

	return ret;
}

int
main(int argc, char *argv[])
{
	FILE *fp;
	char path[PATH_MAX], repodirabs[PATH_MAX + 1];
	const char *repodir;
	int i, ret = 0;

	if (argc < 2) {
		fprintf(stderr, "%s [repodir...]\n", argv[0]);
		return 1;
	}

	git_libgit2_init();

#ifdef __OpenBSD__
	for (i = 1; i < argc; i++)
		if (unveil(argv[i], "r") == -1)
			err(1, "unveil: %s", argv[i]);

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");
#endif

	writeheader(stdout);

	for (i = 1; i < argc; i++) {
		repodir = argv[i];
		if (!realpath(repodir, repodirabs))
			err(1, "realpath");

		if (git_repository_open_ext(&repo, repodir,
		    GIT_REPOSITORY_OPEN_NO_SEARCH, NULL)) {
			fprintf(stderr, "%s: cannot open repository\n", argv[0]);
			ret = 1;
			continue;
		}

		/* use directory name as name */
		if ((name = strrchr(repodirabs, '/')))
			name++;
		else
			name = "";

		/* read description or .git/description */
		joinpath(path, sizeof(path), repodir, "description");
		if (!(fp = fopen(path, "r"))) {
			joinpath(path, sizeof(path), repodir, ".git/description");
			fp = fopen(path, "r");
		}
		description[0] = '\0';
		if (fp) {
			if (!fgets(description, sizeof(description), fp))
				description[0] = '\0';
			fclose(fp);
		}

		/* read owner or .git/owner */
		joinpath(path, sizeof(path), repodir, "owner");
		if (!(fp = fopen(path, "r"))) {
			joinpath(path, sizeof(path), repodir, ".git/owner");
			fp = fopen(path, "r");
		}
		owner[0] = '\0';
		if (fp) {
			if (!fgets(owner, sizeof(owner), fp))
				owner[0] = '\0';
			owner[strcspn(owner, "\n")] = '\0';
			fclose(fp);
		}
		writelog(stdout);
	}
	writefooter(stdout);

	/* cleanup */
	git_repository_free(repo);
	git_libgit2_shutdown();

	return ret;
}
