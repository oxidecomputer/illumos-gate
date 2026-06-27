/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS
#endif /* _FILE_OFFSET_BITS */

#include <sys/contract/process.h>
#include <sys/ctfs.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libcontract.h>
#include <libcontract_priv.h>
#include <libgen.h>
#include <libuutil.h>
#include <limits.h>
#include <poll.h>
#include <procfs.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "startd.h"

void
contract_abandon(ctid_t ctid)
{
	int err;

	assert(ctid != 0);

	err = contract_abandon_id(ctid);

	if (err)
		log_framework(LOG_NOTICE,
		    "failed to abandon contract %ld: %s\n", ctid,
		    strerror(err));
}

int
contract_kill(ctid_t ctid, int sig, const char *fmri)
{
	if (sigsend(P_CTID, ctid, sig) == -1 && errno != ESRCH) {
		log_error(LOG_WARNING,
		    "%s: Could not signal all contract members: %s\n", fmri,
		    strerror(errno));
		return (-1);
	}

	return (0);
}

ctid_t
contract_init()
{
	int psfd, csfd;
	ctid_t ctid, configd_ctid = -1;
	psinfo_t psi;
	ct_stathdl_t s;
	ctid_t *ctids;
	uint_t nctids;
	uint_t n;
	int err;

	/*
	 * 2.  Acquire any contracts we should have inherited.  First, find the
	 * contract we belong to, then get its status.
	 */
	if ((psfd = open("/proc/self/psinfo", O_RDONLY)) < 0) {
		log_error(LOG_WARNING, "Can not open /proc/self/psinfo; unable "
		    "to check to adopt contracts: %s\n", strerror(errno));
		return (-1);
	}

	if (read(psfd, &psi, sizeof (psinfo_t)) != sizeof (psinfo_t)) {
		log_error(LOG_WARNING, "Can not read from /proc/self/psinfo; "
		    "unable to adopt contracts: %s\n",
		    strerror(errno));
		startd_close(psfd);
		return (-1);
	}

	ctid = psi.pr_contract;

	startd_close(psfd);

	if ((csfd = contract_open(ctid, "process", "status", O_RDONLY)) < 0) {
		log_error(LOG_WARNING, "Can not open containing contract "
		    "status; unable to adopt contracts: %s\n", strerror(errno));
		return (-1);
	}

	/* 3.  Go about adopting our member list. */

	err = ct_status_read(csfd, CTD_ALL, &s);
	startd_close(csfd);
	if (err) {
		log_error(LOG_WARNING, "Can not read containing contract "
		    "status; unable to adopt: %s\n", strerror(err));
		return (-1);
	}

	if (err = ct_pr_status_get_contracts(s, &ctids, &nctids)) {
		log_error(LOG_WARNING, "Can not get my inherited contracts; "
		    "unable to adopt: %s\n", strerror(err));
		ct_status_free(s);
		return (-1);
	}

	if (nctids == 0) {
		/*
		 * We're booting, as a svc.startd which managed to fork a
		 * child will always have a svc.configd contract to adopt.
		 */
		st->st_initial = 1;
		ct_status_free(s);
		return (-1);
	}

	/*
	 * We're restarting after an interruption of some kind.
	 */
	log_framework(LOG_NOTICE, "restarting after interruption\n");
	st->st_initial = 0;

	/*
	 * 3'.  Loop through the array, adopting them all where possible, and
	 * noting which one contains svc.configd (via a cookie vlaue of
	 * CONFIGD_COOKIE).
	 */
	for (n = 0; n < nctids; n++) {
		int ccfd;
		ct_stathdl_t cs;

		if ((ccfd = contract_open(ctids[n], "process", "ctl",
		    O_WRONLY)) < 0) {
			log_error(LOG_WARNING, "Can not open contract %ld ctl "
			    "for adoption: %s\n", ctids[n], strerror(err));

			continue;
		}

		if ((csfd = contract_open(ctids[n], "process", "status",
		    O_RDONLY)) < 0) {
			log_error(LOG_WARNING, "Can not open contract %ld "
			    "status for cookie: %s\n", ctids[n], strerror(err));
			startd_close(ccfd);

			continue;
		}

		if (err = ct_ctl_adopt(ccfd)) {
			log_error(LOG_WARNING, "Can not adopt contract %ld: "
			    "%s\n", ctids[n], strerror(err));
			startd_close(ccfd);
			startd_close(csfd);

			continue;
		}

		startd_close(ccfd);

		if (err = ct_status_read(csfd, CTD_COMMON, &cs)) {
			log_error(LOG_WARNING, "Can not read contract %ld"
			    "status; unable to fetch cookie: %s\n", ctids[n],
			    strerror(err));

			ct_status_free(cs);
			startd_close(csfd);

			continue;
		}

		if (ct_status_get_cookie(cs) == CONFIGD_COOKIE)
			configd_ctid = ctids[n];

		ct_status_free(cs);

		startd_close(csfd);
	}

	ct_status_free(s);

	return (configd_ctid);
}

int
contract_is_empty(ctid_t ctid)
{
	int fd;
	ct_stathdl_t ctstat;
	pid_t *members;
	uint_t num;
	int ret;

	fd = contract_open(ctid, "process", "status", O_RDONLY);
	if (fd < 0)
		return (1);

	ret = ct_status_read(fd, CTD_ALL, &ctstat);
	(void) close(fd);
	if (ret != 0)
		return (1);

	ret = ct_pr_status_get_members(ctstat, &members, &num);
	ct_status_free(ctstat);
	if (ret != 0)
		return (1);

	if (num == 0)
		return (1);
	else
		return (0);
}

typedef struct contract_bucket {
	pthread_mutex_t cb_lock;
	uu_list_t	*cb_list;
} contract_bucket_t;

#define	CI_HASH_SIZE	64
#define	CI_HASH_MASK	(CI_HASH_SIZE - 1);

/*
 * contract_hash is a hash table of contract ids to restarter instance
 * IDs.  It can be used for quick lookups when processing contract events,
 * because the restarter instance lock doesn't need to be held to access
 * its entries.
 */
static contract_bucket_t contract_hash[CI_HASH_SIZE];

static contract_bucket_t *
contract_hold_bucket(ctid_t ctid)
{
	contract_bucket_t *bp;
	int hash;

	hash = ctid & CI_HASH_MASK;

	bp = &contract_hash[hash];
	MUTEX_LOCK(&bp->cb_lock);
	return (bp);
}

static void
contract_release_bucket(contract_bucket_t *bp)
{
	assert(MUTEX_HELD(&bp->cb_lock));
	MUTEX_UNLOCK(&bp->cb_lock);
}

static contract_entry_t *
contract_lookup(contract_bucket_t *bp, ctid_t ctid)
{
	contract_entry_t *ce;

	assert(MUTEX_HELD(&bp->cb_lock));

	if (bp->cb_list == NULL)
		return (NULL);

	for (ce = uu_list_first(bp->cb_list); ce != NULL;
	    ce = uu_list_next(bp->cb_list, ce)) {
		if (ce->ce_ctid == ctid)
			return (ce);
	}

	return (NULL);
}

static void
contract_insert(contract_bucket_t *bp, contract_entry_t *ce)
{
	int r;

	if (bp->cb_list == NULL)
		bp->cb_list = startd_list_create(contract_list_pool, bp, 0);

	uu_list_node_init(ce, &ce->ce_link, contract_list_pool);
	r = uu_list_insert_before(bp->cb_list, NULL, ce);
	assert(r == 0);
}

void
contract_hash_init()
{
	int i;

	for (i = 0; i < CI_HASH_SIZE; i++)
		(void) pthread_mutex_init(&contract_hash[i].cb_lock,
		    &mutex_attrs);
}

void
contract_hash_store(ctid_t ctid, int instid)
{
	contract_bucket_t *bp;
	contract_entry_t *ce;

	bp = contract_hold_bucket(ctid);
	assert(contract_lookup(bp, ctid) == NULL);
	ce = startd_alloc(sizeof (contract_entry_t));
	ce->ce_ctid = ctid;
	ce->ce_instid = instid;

	contract_insert(bp, ce);

	contract_release_bucket(bp);
}

void
contract_hash_remove(ctid_t ctid)
{
	contract_bucket_t *bp;
	contract_entry_t *ce;

	bp = contract_hold_bucket(ctid);

	ce = contract_lookup(bp, ctid);
	if (ce != NULL) {
		uu_list_remove(bp->cb_list, ce);
		startd_free(ce, sizeof (contract_entry_t));
	}

	contract_release_bucket(bp);
}

/*
 * int lookup_inst_by_contract()
 *   Lookup the instance id in the hash table by the contract id.
 *   Returns instid if found, -1 if not.  Doesn't do a hold on the
 *   instance, so a check for continued existence is required.
 */
int
lookup_inst_by_contract(ctid_t ctid)
{
	contract_bucket_t *bp;
	contract_entry_t *ce;
	int id = -1;

	bp = contract_hold_bucket(ctid);
	ce = contract_lookup(bp, ctid);
	if (ce != NULL)
		id = ce->ce_instid;
	contract_release_bucket(bp);

	return (id);
}

#define DEBUG_OUTPUT_ROOT 				"/var/svc/debug"
#define DEBUG_DEFAULT_TIMEOUT_SECONDS	30
#define DEBUG_MAX_TIMEOUT_SECONDS		120
#define DEBUG_DEFAULT_HELPER_TOKEN	    "core"
#define DEBUG_PG						"debug_on_timeout"
#define DEBUG_PROPERTY_ENABLED			"enabled"
#define DEBUG_PROPERTY_TIMEOUT_SECONDS	"timeout_seconds"
#define DEBUG_PROPERTY_HELPERS			"helpers"
#define MAX_HELPER_ARGC					8

/*
 * A table of supported debug output data to collect.
 *
 * SMF manifests list the `dhd_token` element only, from which we find the
 * rest of the details needed to actually run the helper. At most 1 extra
 * argument is supported right now.
 */
typedef struct debug_helper_def {
	const char *dhd_token;
	const char *dhd_path;
	const char *dhd_arg;
} debug_helper_def_t;

static const debug_helper_def_t debug_helpers[] = {
	{ "core", "/usr/bin/gcore", NULL },
	{ "pstack", "/usr/bin/pstack", NULL },
	{ "pfiles", "/usr/bin/pfiles", NULL },
	{ "pflags", "/usr/bin/pflags", NULL },
	{ "pmap", "/usr/bin/pmap", "-x" },
	{ "pargs", "/usr/bin/pargs", NULL },
	{ NULL, NULL, NULL }
};

typedef struct debug_on_timeout_cfg {
	boolean_t					dot_enabled;
	uint64_t					dot_timeout_seconds;
	const debug_helper_def_t 	**dot_helpers;
	size_t					 	dot_helpers_size;
} debug_on_timeout_cfg_t;

/*
 * Queue for collecting debug data when a method times out.
 */
typedef struct debug_queue {
	uu_list_t		*dq_list;
	pthread_mutex_t	dq_lock;
	pthread_cond_t	dq_cv;
} debug_queue_t;

typedef struct debug_entry {
	ctid_t			de_ctid;
	char			*de_fmri;
	char			*de_logstem;
	uu_list_node_t	de_link;
} debug_entry_t;

/*
 * Queue for items we want to collect debug-on-timeout data from.
 */
static debug_queue_t *debug_queue;
static uu_list_pool_t *debug_pool;

/*
 * Return true if the dump-on-timeout SMF property exists and is enabled.
 */
boolean_t
contract_collect_debug_on_timeout_enabled(const char *fmri)
{
	scf_simple_prop_t *sp;
	uint8_t *v;
	boolean_t enabled = B_FALSE;
	if ((sp = scf_simple_prop_get(NULL, fmri, DEBUG_PG,
	    DEBUG_PROPERTY_ENABLED)) != NULL) {
		if ((v = scf_simple_prop_next_boolean(sp)) != NULL)
			enabled = (*v) ? B_TRUE : B_FALSE;
		scf_simple_prop_free(sp);
	}
	return (enabled);
}

/*
 * Add an item to the debug-on-timeout queue and signal the worker.
 */
void
contract_collect_debug_enqueue(ctid_t ctid, const char *fmri,
    const char *logstem)
{
	debug_entry_t *de;

	de = startd_alloc(sizeof (*de));
	de->de_ctid = ctid;
	de->de_fmri = safe_strdup(fmri);
	de->de_logstem = safe_strdup(logstem);
	uu_list_node_init(de, &de->de_link, debug_pool);

	(void) pthread_mutex_lock(&debug_queue->dq_lock);
	(void) uu_list_insert_after(debug_queue->dq_list, NULL, de);
	(void) pthread_cond_signal(&debug_queue->dq_cv);
	(void) pthread_mutex_unlock(&debug_queue->dq_lock);
}

/*
 * Lookup a debug-on-timeout helper by its token.
 */
static const debug_helper_def_t *
helper_lookup(const char *token)
{
	const debug_helper_def_t *h;
	for (h = debug_helpers; h->dhd_token != NULL; h++) {
		if (strcmp(h->dhd_token, token) == 0)
			return (h);
	}
	return (NULL);
}

/*
 * Read the debug-on-timeout configuration for an FMRI.
 *
 * Return 0 on success, or -1 if the configuration could not be read.
 * If the debug-on-timeout property group does not exist, or the "enabled"
 * property is missing or false, 0 is returned and `cfg.dot_enabled` is
 * false. The rest of the fields of `cfg` are 0 or NULL, and should not
 * be read.
 *
 * The caller is responsible for freeing `cfg` with
 * `debug_on_timeout_free_cfg()`.
 */
static int
debug_on_timeout_read_cfg(const char *fmri, debug_on_timeout_cfg_t *cfg)
{
	scf_handle_t *h;
	scf_simple_prop_t *sp;
	int ret = 0;
	uint8_t *bv;
	uint64_t *cv;
	char *sv;

	bzero(cfg, sizeof (*cfg));
	cfg->dot_timeout_seconds = DEBUG_DEFAULT_TIMEOUT_SECONDS;

	if ((h = scf_handle_create(SCF_VERSION)) == NULL)
		return (-1);
	if (scf_handle_bind(h) != 0) {
		ret = -1;
		goto out;
	}

	/*
	 * Check if the PG exists and is enabled. Don't bother to read the rest of
	 * the properties if doesn't exist or is not enabled.
	 */
	sp = scf_simple_prop_get(h, fmri, DEBUG_PG, DEBUG_PROPERTY_ENABLED);
	if (sp != NULL) {
		if ((bv = scf_simple_prop_next_boolean(sp)) != NULL)
			cfg->dot_enabled = (*bv) ? B_TRUE : B_FALSE;
		scf_simple_prop_free(sp);
	}
	if (!cfg->dot_enabled) {
		ret = 0;
		goto out;
	}

	/*
	 * Read the timeout property, clamping it to the max.
	 */
	sp = scf_simple_prop_get(h, fmri, DEBUG_PG, DEBUG_PROPERTY_TIMEOUT_SECONDS);
	if (sp != NULL) {
		if ((cv = scf_simple_prop_next_count(sp)) != NULL)
			cfg->dot_timeout_seconds = *cv;
		scf_simple_prop_free(sp);
	}
	if (cfg->dot_timeout_seconds < 1 ||
	    cfg->dot_timeout_seconds > DEBUG_MAX_TIMEOUT_SECONDS) {
		log_framework(LOG_WARNING, "%s: debug_on_timeout/timeout_seconds "
		    "%llu is outside allowed range [1, %d], it will be clamped\n",
		    fmri, (unsigned long long)cfg->dot_timeout_seconds,
		    DEBUG_MAX_TIMEOUT_SECONDS);
		cfg->dot_timeout_seconds = MIN(MAX(1, cfg->dot_timeout_seconds),
		    DEBUG_MAX_TIMEOUT_SECONDS);
	}

	/*
	 * Extract the list of helpers.
	 */
	sp = scf_simple_prop_get(h, fmri, DEBUG_PG, DEBUG_PROPERTY_HELPERS);
	if (sp != NULL) {
		ssize_t n = scf_simple_prop_numvalues(sp);
		if (n > 0) {
			cfg->dot_helpers_size = (n + 1) * sizeof (debug_helper_def_t *);
			cfg->dot_helpers = startd_zalloc(cfg->dot_helpers_size);
			size_t out = 0, i = 0;
			for (i = 0; i < n; i++) {
				const debug_helper_def_t *hd;
				sv = scf_simple_prop_next_astring(sp);
				if (sv == NULL)
					break;
				if ((hd = helper_lookup(sv)) == NULL) {
					log_framework(LOG_WARNING, "%s: unknown "
					    "helper token \"%s\", ignoring\n",
						fmri, sv);
					continue;
				}
				cfg->dot_helpers[out++] = hd;
			}
        }
		scf_simple_prop_free(sp);
	}
	/*
	 * If no helper explicitly listed, or all are invalid, default to only a
     * core.
	 */
	if (cfg->dot_helpers == NULL || cfg->dot_helpers[0] == NULL) {
		if (cfg->dot_helpers == NULL) {
			cfg->dot_helpers_size = 2 * sizeof (debug_helper_def_t *);
			cfg->dot_helpers = startd_zalloc(cfg->dot_helpers_size);
		}
		cfg->dot_helpers[0] = helper_lookup(DEBUG_DEFAULT_HELPER_TOKEN);
	}

out:
	scf_handle_destroy(h);
	return (ret);
}

static void
debug_on_timeout_free_cfg(debug_on_timeout_cfg_t *cfg)
{
	/*
	 * The elements of the `dot_helpers` array point to the static
	 * `debug_helpers` table, and must not be freed.
	 */
	if (cfg->dot_helpers != NULL)
		startd_free(cfg->dot_helpers, cfg->dot_helpers_size);
}

/*
 * Activate a new contract template, into which a debug helper process is
 * spawned. This prevents spawning the helpers in the contracts managed by
 * `svc.startd` itself, and so getting events from them delivered to its
 * routines for handling "normal" SMF services.
 */
static int
debug_helper_template_activate(void)
{
	int fd;
	if ((fd = open64(CTFS_ROOT "/process/template", O_RDWR)) == -1) {
		return (-1);
	}
	/*
	 * Do not orphan any children the helpers may spawn.
	 */
	if (ct_tmpl_set_critical(fd, 0) != 0 ||
	    ct_tmpl_set_informative(fd, 0) != 0 ||
		ct_pr_tmpl_set_fatal(fd, CT_PR_EV_HWERR) ||
        ct_pr_tmpl_set_param(fd, CT_PR_PGRPONLY | CT_PR_REGENT) != 0 ||
	    ct_tmpl_activate(fd) != 0) {
		(void) close(fd);
		return (-1);
	}
	return (fd);
}

/*
 * Run a single debug-on-timeout helper and record its output.
 */
static void
run_debug_helper(const debug_helper_def_t *hp, pid_t target, const char *dir,
    uint64_t timeout_seconds, const char *fmri)
{
	char pidstr[16];
	char outpath[PATH_MAX];
	char prefix[PATH_MAX];
	char *argv[MAX_HELPER_ARGC];
	int argc = 0;
	int outfd = -1;
	int ctfd;
	pid_t pid;
	ctid_t ct;
	uint64_t deadline_ms = timeout_seconds * 1000;
	uint64_t waited;

	(void) snprintf(pidstr, sizeof (pidstr), "%ld", (long)target);

	/*
	 * Construct the arguments for the helper we're spawning.
	 */
	argv[argc++] = (char *)hp->dhd_path;
	if (strcmp(hp->dhd_token, "core") == 0) {
		/*
		 * We use `gcore -o <prefix>`, to which the binary already appends the
		 * PID directly.
		 */
		(void) snprintf(prefix, sizeof (prefix), "%s/%s", dir, hp->dhd_token);
		argv[argc++] = "-o";
		argv[argc++] = prefix;
		argv[argc++] = pidstr;
		argv[argc] = NULL;
	} else {
		/*
		 * Other helpers use the full outpath, since we explicitly redirect
		 * their output into a file like `<helper>.<pid>` to match `gcore`.
		 */
		if (hp->dhd_arg != NULL)
			argv[argc++] = (char *)hp->dhd_arg;
		argv[argc++] = pidstr;
		argv[argc] = NULL;
		(void) snprintf(outpath, sizeof (outpath), "%s/%s.%ld", dir,
		    hp->dhd_token, (long)target);
		outfd = open(outpath, O_WRONLY | O_CREAT | O_EXCL,
		    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (outfd == -1) {
			log_framework(LOG_WARNING, "%s: cannot create output file "
			    "%s: %s, skipping %s for pid %ld\n", fmri, outpath,
				strerror(errno), hp->dhd_token, (long)target);
			return;
		}
	}

	/*
	 * Activate the new contract template and spawn the helper in it.
	 */
	if ((ctfd = debug_helper_template_activate()) == -1) {
		log_framework(LOG_WARNING, "%s: cannot activate contract "
		    "template for debug helper %s: %s\n",
			fmri, hp->dhd_token, strerror(errno));
		if (outfd >= 0)
			(void) close(outfd);
		return;
	}
	pid = fork();
	if (pid == -1) {
		(void) ct_tmpl_clear(ctfd);
		(void) close(ctfd);
		if (outfd >= 0)
			(void) close(outfd);
		log_framework(LOG_WARNING, "%s: failed to fork helper %s: %s\n",
		    fmri, hp->dhd_token, strerror(errno));
		return;
	}

	if (pid == 0) {
		/*
		 * Child
		 */
		sigset_t empty;
		(void) sigemptyset(&empty);
		(void) sigprocmask(SIG_SETMASK, &empty, NULL);
		if (outfd >= 0) {
			if (dup2(outfd, STDOUT_FILENO) == -1 ||
			    dup2(outfd, STDERR_FILENO) == -1) {
				if (outfd >= 0)
					(void) close(outfd);
				_exit(EXIT_FAILURE);
			}
			if (outfd > STDERR_FILENO)
				(void) close(outfd);
		}
		(void) close(STDIN_FILENO);
		(void) open("/dev/null", O_RDONLY);
		(void) execve(argv[0], argv, NULL);
		_exit(EXIT_FAILURE);
	}

	/*
	 * Wait for the child in the parent, up to the timeout.
	 */
	(void) ct_tmpl_clear(ctfd);
	(void) close(ctfd);
	if (outfd >= 0)
		(void) close(outfd);
	if (contract_latest(&ct) == 0)
		(void) contract_abandon_id(ct);
	for (waited = 0; waited < deadline_ms; waited += 100) {
		int status;
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid)
			return;
		if (r == -1 && errno != EINTR) {
			log_framework(LOG_WARNING, "%s: waitpid failed for "
			    "helper %s pid %ld: %s\n", fmri, hp->dhd_token,
			    (long)pid, strerror(errno));
			return;
		}
		(void) poll(NULL, 0, 100);
	}

	log_framework(LOG_WARNING, "%s: helper %s for pid %ld "
	    "exceeded timeout %llu, killing pid %ld\n",
	    fmri, hp->dhd_token, (long)target,
	    (unsigned long long)timeout_seconds, (long)pid);
	(void) kill(pid, SIGKILL);
	(void) waitpid(pid, NULL, 0);
}

/*
 * Collect debug information for one FMRI.
 *
 * For every process in the contract, run all the configured helpers on them and
 * record the output to disk.
 */
static void
contract_collect_debug(ctid_t ctid, const char *fmri, const char *logstem,
    debug_on_timeout_cfg_t *cfg)
{
	char dir[PATH_MAX];
	char fmri_path[PATH_MAX];
	int statfd = -1, err;
	ct_stathdl_t s = NULL;
	const debug_helper_def_t **hp;
	pid_t *pids;
	uint_t npids, i;
	size_t logstem_len = strlen(logstem);
	const size_t suffix_len = sizeof (LOG_SUFFIX) - 1;

	/*
	 * Create a per-FMRI directory name using the logstem. This has already had
	 * path separators replaced with '-' and is suitable for a dirname.
	 */
	if (logstem_len > suffix_len &&
	    strcmp(logstem + logstem_len - suffix_len, LOG_SUFFIX) == 0) {
		(void) snprintf(fmri_path, sizeof (fmri_path), "%.*s",
		    (int)(logstem_len - suffix_len), logstem);
	} else {
		(void) strlcpy(fmri_path, logstem, sizeof (fmri_path));
	}
	(void) snprintf(dir, sizeof (dir), "%s/%s.%d.%ld", DEBUG_OUTPUT_ROOT,
	    fmri_path, ctid, time(NULL));
	if (mkdirp(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
		log_framework(LOG_WARNING, "%s: cannot create output "
		    "directory '%s': %s\n", fmri, dir, strerror(errno));
		goto out;
	}

	/*
	 * List all the PIDs in the contract.
	 */
	if ((statfd = contract_open(ctid, "process", "status", O_RDONLY)) < 0) {
		log_framework(LOG_WARNING, "%s: contract_open(%d): %s\n",
		    fmri, ctid, strerror(errno));
		goto out;
	}
	if ((err = ct_status_read(statfd, CTD_ALL, &s)) != 0) {
		log_framework(LOG_WARNING, "%s: ct_status_read(%d): %s\n",
		    fmri, ctid, strerror(err));
		goto out;
	}
	if ((err = ct_pr_status_get_members(s, &pids, &npids)) != 0) {
		log_framework(LOG_WARNING, "%s: ct_pr_status_get_members(%d): %s\n",
		    fmri, ctid, strerror(err));
		goto out;
	}

	/*
	 * Collect the output of each helper from each PID.
	 */
	log_framework(LOG_NOTICE, "%s: collecting debug data for contract "
	    "%d (%u PIDs) into %s\n", fmri, ctid, npids, dir);
	for (i = 0; i < npids; i++) {
		for (hp = cfg->dot_helpers; *hp != NULL; hp++) {
			run_debug_helper(*hp, pids[i], dir, cfg->dot_timeout_seconds,
			    fmri);
		}
	}

out:
	if (s != NULL)
		ct_status_free(s);
	if (statfd >= 0)
		startd_close(statfd);
}

/*
 * Worker which processes debug-on-timeout items and then kills the contract.
 */
/*ARGSUSED*/
static void *
contract_collect_debug_thread(void *arg)
{
	(void) pthread_setname_np(pthread_self(), "collect_debug");
	for (;;) {
		debug_entry_t *de = NULL;
		debug_on_timeout_cfg_t cfg;

		/*
		 * Wait for an item to process on the queue.
		 */
		(void) pthread_mutex_lock(&debug_queue->dq_lock);
		while ((de = uu_list_first(debug_queue->dq_list)) == NULL)
		    (void) pthread_cond_wait(&debug_queue->dq_cv,
			    &debug_queue->dq_lock);
		uu_list_remove(debug_queue->dq_list, de);
		(void) pthread_mutex_unlock(&debug_queue->dq_lock);

		/*
		 * Read the debug-on-timeout configuration from the FMRI itself,
		 * to see what data we want to collect.
		 */
		if (debug_on_timeout_read_cfg(de->de_fmri, &cfg) == 0 &&
		    cfg.dot_enabled) {
			contract_collect_debug(de->de_ctid, de->de_fmri,
			    de->de_logstem, &cfg);
		}
		debug_on_timeout_free_cfg(&cfg);

		/*
		 * Actually kill the contract and cleanup.
		 */
		(void) contract_kill(de->de_ctid, SIGKILL, de->de_fmri);
		free(de->de_fmri);
		free(de->de_logstem);
		startd_free(de, sizeof (*de));
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Set up the debug-on-timeout queue and start the worker thread.
 */
void
contract_collect_debug_init()
{
	debug_queue = startd_zalloc(sizeof (debug_queue_t));

	(void) pthread_mutex_init(&debug_queue->dq_lock, &mutex_attrs);
	(void) pthread_cond_init(&debug_queue->dq_cv, NULL);

	debug_pool = startd_list_pool_create("debug_queue",
	    sizeof (debug_entry_t), offsetof(debug_entry_t, de_link),
	    NULL, 0);
	assert(debug_pool != NULL);

	debug_queue->dq_list = startd_list_create(debug_pool, NULL, 0);
	assert(debug_queue->dq_list != NULL);

	(void) startd_thread_create(contract_collect_debug_thread, NULL);
}
