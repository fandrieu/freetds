/*
 * Purpose: Test RPC BLOB output with TDS > 7.2
 * Functions:  dbretdata dbretlen dbrettype dbrpcinit dbrpcparam dbrpcsend
 */

#include "common.h"
/* default "text size" in freetds.conf is 64512,
   test with something smaller than that but larger then 8000 */
#define PROC_PADLEN 60000
#define PROC_TSTLEN 7
#define PROC_TST "test-OK"

static RETCODE init_proc(DBPROCESS * dbproc, const char *name);
int ignore_err_handler(DBPROCESS * dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr);
int ignore_msg_handler(DBPROCESS * dbproc, DBINT msgno, int state, int severity, char *text, char *server, char *proc, int line);

static RETCODE
init_proc(DBPROCESS * dbproc, const char *name)
{
	RETCODE ret = FAIL;

	if (name[0] != '#') {
		printf("Dropping procedure %s\n", name);
		sql_cmd(dbproc);
		dbsqlexec(dbproc);
		while (dbresults(dbproc) != NO_MORE_RESULTS) {
			/* nop */
		}
	}

	printf("Creating procedure %s\n", name);
	sql_cmd(dbproc);
	if ((ret = dbsqlexec(dbproc)) == FAIL) {
		if (name[0] == '#')
			printf("Failed to create procedure %s. Wrong permission or not MSSQL.\n", name);
		else
			printf("Failed to create procedure %s. Wrong permission.\n", name);
	}
	while (dbresults(dbproc) != NO_MORE_RESULTS) {
		/* nop */
	}
	return ret;
}

static int failed = 0;

int
ignore_msg_handler(DBPROCESS * dbproc, DBINT msgno, int state, int severity, char *text, char *server, char *proc, int line)
{
	int ret;

	dbsetuserdata(dbproc, (BYTE*) &msgno);
	/* printf("(ignoring message %d)\n", msgno); */
	ret = syb_msg_handler(dbproc, msgno, state, severity, text, server, proc, line);
	dbsetuserdata(dbproc, NULL);
	return ret;
}
/*
 * The bad procedure name message has severity 15, causing db-lib to call the error handler after calling the message handler.
 * This wrapper anticipates that behavior, and again sets the userdata, telling the handler this error is expected. 
 */
int
ignore_err_handler(DBPROCESS * dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr)
{	
	int erc;
	static int recursion_depth = 0;
	
	if (dbproc == NULL) {	
		printf("expected error %d: \"%s\"\n", dberr, dberrstr? dberrstr : "");
		return INT_CANCEL;
	}
	
	if (recursion_depth++) {
		printf("error %d: \"%s\"\n", dberr, dberrstr? dberrstr : "");
		printf("logic error: recursive call to ignore_err_handler\n");
		exit(1);
	}
	dbsetuserdata(dbproc, (BYTE*) &dberr);
	/* printf("(ignoring error %d)\n", dberr); */
	erc = syb_err_handler(dbproc, severity, dberr, oserr, dberrstr, oserrstr);
	dbsetuserdata(dbproc, NULL);
	recursion_depth--;
	return erc;
}


struct parameters_t {
	const char   *name;
	BYTE         status;
	int          type;
	DBINT        maxlen;
	DBINT        datalen;
	BYTE         *value;
};

#define PARAM_STR(s) sizeof(s)-1, (BYTE*) s
static struct parameters_t bindings[] = {
	  { "", DBRPCRETURN, SYBTEXT, -1, 0, NULL }	/* maxlen has no effect ? but "text size" has */
	, { NULL, 0, 0, 0, 0, NULL }
};

static void
bind_param(DBPROCESS *dbproc, struct parameters_t *pb)
{
	RETCODE erc;
	const char *name = pb->name[0] ? pb->name : NULL;

	if ((erc = dbrpcparam(dbproc, name, pb->status, pb->type, pb->maxlen, pb->datalen, pb->value)) == FAIL) {
		fprintf(stderr, "Failed line %d: dbrpcparam\n", __LINE__);
		failed++;
	}
}

int
main(int argc, char **argv)
{
	LOGINREC *login;
	DBPROCESS *dbproc;

	char teststr[PROC_PADLEN+PROC_TSTLEN+1];
	int i;
	int rettype = 0, retlen = 0;
	char proc[] = "#rpc_blob_out";
	char *proc_name = proc;

	struct parameters_t *pb;

	RETCODE erc;

#ifndef DBTDS_7_2
	/* blob output not supported by TDS < 7.2 */
	printf("%s OK\n", __FILE__);
	return 0;
#endif

	set_malloc_options();

	read_login_info(argc, argv);

	printf("Starting %s\n", argv[0]);

	dbinit();

	dberrhandle(syb_err_handler);
	dbmsghandle(syb_msg_handler);

	printf("About to logon\n");

	login = dblogin();
	DBSETLPWD(login, PASSWORD);
	DBSETLUSER(login, USER);
	DBSETLAPP(login, "rpc_blob_out");

	printf("About to open %s.%s\n", SERVER, DATABASE);

	dbproc = dbopen(login, SERVER);
	if (strlen(DATABASE))
		dbuse(dbproc, DATABASE);
	dbloginfree(login);

	if (dbtds(dbproc) < DBTDS_7_2) {
		fprintf(stderr, "Failed line %d: tds version < 7.2\n", __LINE__);
		exit(1);
	}

	dberrhandle(ignore_err_handler);
	dbmsghandle(ignore_msg_handler);

	printf("trying to create a temporary stored procedure\n");
	if (FAIL == init_proc(dbproc, proc_name)) {
		printf("trying to create a permanent stored procedure\n");
		if (FAIL == init_proc(dbproc, ++proc_name))
			exit(EXIT_FAILURE);
	}

	dberrhandle(syb_err_handler);
	dbmsghandle(syb_msg_handler);

	printf("Created procedure %s\n", proc_name);

	erc = dbrpcinit(dbproc, proc_name, 0);	/* no options */
	if (erc == FAIL) {
		fprintf(stderr, "Failed line %d: dbrpcinit\n", __LINE__);
		failed = 1;
	}
	for (pb = bindings; pb->name != NULL; pb++)
		bind_param(dbproc, pb);
	erc = dbrpcsend(dbproc);
	if (erc == FAIL) {
		fprintf(stderr, "Failed line %d: dbrpcsend\n", __LINE__);
		exit(1);
	}
	while (dbresults(dbproc) != NO_MORE_RESULTS)
		continue;
	if (dbnumrets(dbproc) != 1) {	/* dbnumrets missed something */
		fprintf(stderr, "Expected 1 output parameters.\n");
		exit(1);
	}
	i = 1;
	rettype = dbrettype(dbproc, i);
	retlen = dbretlen(dbproc, i);
	dbconvert(dbproc, rettype, dbretdata(dbproc, i), retlen, SYBVARCHAR, (BYTE*) teststr, -1);
	if (retlen != PROC_PADLEN+PROC_TSTLEN) {
		fprintf(stderr, "Unexpected output length %d.\n", retlen);
		exit(1);
	}
	if (teststr[0] != 'p') {	/* "pad' */
		fprintf(stderr, "Unexpected first char 0x%x.\n", teststr[0]);
		exit(1);
	}
	if (strcmp(teststr+PROC_PADLEN, PROC_TST) != 0) {
		fprintf(stderr, "Unexpected '%s' results.\n", teststr+PROC_PADLEN);
		exit(1);
	}

	dbexit();

	printf("%s %s\n", __FILE__, (failed ? "failed!" : "OK"));

	return failed ? 1 : 0;
}
