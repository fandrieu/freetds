/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005  Brian Bruns
 * Copyright (C) 2011  Frediano Ziglio
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <stdio.h>
#include <ctype.h>

#if HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

/* These should be in stdlib */
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_STRINGS_H
#include <strings.h>
#endif /* HAVE_STRINGS_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_LOCALE_H
#include <locale.h>
#endif

#include <freetds/tds.h>
#include <freetds/utils.h>
#include "replacements.h"
#include <sybfront.h>
#include <sybdb.h>
#include "freerpc.h"

void pusage(void);
int process_parameters(int, char **, struct pd *);
int login_to_database(struct pd *, DBPROCESS **);

int do_rpc(RPCPARAMDATA * pdata, DBPROCESS * dbproc);
int setoptions (DBPROCESS * dbproc, RPCPARAMDATA * params);

int err_handler(DBPROCESS * dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr);
int msg_handler(DBPROCESS * dbproc, DBINT msgno, int msgstate, int severity, char *msgtext, char *srvname, char *procname,
		int line);

int
main(int argc, char **argv)
{
	RPCPARAMDATA params;
	DBPROCESS *dbproc;

	setlocale(LC_ALL, "");

#ifdef __VMS
	/* Convert VMS-style arguments to Unix-style */
	parse_vms_args(&argc, &argv);
#endif

	memset(&params, '\0', sizeof(params));

	params.textsize = 2147483647;	/* our default text size is the LARGEST */

	if (process_parameters(argc, argv, &params) == FALSE) {
		exit(EXIT_FAILURE);
	}
	if (getenv("FREERPC")) {
		fprintf(stderr, "User name: \"%s\"\n", params.user);
	}


	if (login_to_database(&params, &dbproc) == FALSE) {
		exit(EXIT_FAILURE);
	}

	if (!setoptions(dbproc, &params))
		return FALSE;

	if (!do_rpc(&params, dbproc)) {
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);

	return 0;
}

int
process_parameters(int argc, char **argv, RPCPARAMDATA *pdata)
{
	extern char *optarg;
	extern int optind;
	extern int optopt;

	int ch;
	if (argc < 2) {
		pusage();
		return (FALSE);
	}

	/* argument 1 - the stored procedure name */
	pdata->spname = strdup(argv[1]);
	if (pdata->spname == NULL) {
		fprintf(stderr, "Out of memory!\n");
		return FALSE;
	}

	/*
	 * Get the rest of the arguments
	 */
	optind = 2; /* start processing options after spname */
	while ((ch = getopt(argc, argv, "U:P:I:S:T:A:O:0:C:dvVD:")) != -1) {
		switch (ch) {
		case 'v':
		case 'V':
			printf("freerpc version %s\n", TDS_VERSION_NO);
			return FALSE;
			break;
		case 'd':
			tdsdump_open(NULL);
			break;
		case 'U':
			pdata->Uflag++;
			pdata->user = strdup(optarg);
			break;
		case 'P':
			pdata->Pflag++;
			pdata->pass = tds_getpassarg(optarg);
			break;
		case 'I':
			pdata->Iflag++;
			free(pdata->interfacesfile);
			pdata->interfacesfile = strdup(optarg);
			break;
		case 'S':
			pdata->Sflag++;
			pdata->server = strdup(optarg);
			break;
		case 'D':
			pdata->dbname = strdup(optarg);
			break;
		case 'O':
		case '0':
			pdata->options = strdup(optarg);
			break;
		case 'T':
			pdata->Tflag++;
			pdata->textsize = atoi(optarg);
			break;
		case 'A':
			pdata->Aflag++;
			pdata->packetsize = atoi(optarg);
			break;
		case 'C':
			pdata->charset = strdup(optarg);
			break;
		case '?':
		default:
			pusage();
			return (FALSE);
		}
	}

	/*
	 * Check for required/disallowed option combinations
	 * If no username is provided, rely on domain login.
	 */

	/* Server */
	if (!pdata->Sflag) {
		if ((pdata->server = getenv("DSQUERY")) != NULL) {
			pdata->server = strdup(pdata->server);	/* can be freed */
			pdata->Sflag++;
		} else {
			fprintf(stderr, "-S must be supplied.\n");
			return (FALSE);
		}
	}

	return (TRUE);
}

int
login_to_database(RPCPARAMDATA * pdata, DBPROCESS ** pdbproc)
{
	LOGINREC *login;

	/* Initialize DB-Library. */

	if (dbinit() == FAIL)
		return (FALSE);

	/*
	 * Install the user-supplied error-handling and message-handling
	 * routines. They are defined at the bottom of this source file.
	 */

	dberrhandle(err_handler);
	dbmsghandle(msg_handler);

	/* If the interfaces file was specified explicitly, set it. */
	if (pdata->interfacesfile != NULL)
		dbsetifile(pdata->interfacesfile);

	/*
	 * Allocate and initialize the LOGINREC structure to be used
	 * to open a connection to SQL Server.
	 */

	login = dblogin();
	if (!login)
		return FALSE;

	if (pdata->user)
		DBSETLUSER(login, pdata->user);
	if (pdata->pass) {
		DBSETLPWD(login, pdata->pass);
		memset(pdata->pass, 0, strlen(pdata->pass));
	}

	DBSETLAPP(login, "FreeRPC");
	if (pdata->charset)
		DBSETLCHARSET(login, pdata->charset);

	if (pdata->Aflag && pdata->packetsize > 0) {
		DBSETLPACKET(login, pdata->packetsize);
	}

	if (pdata->dbname)
		DBSETLDBNAME(login, pdata->dbname);

	/*
	 * Get a connection to the database.
	 */

	if ((*pdbproc = dbopen(login, pdata->server)) == NULL) {
		fprintf(stderr, "Can't connect to server \"%s\".\n", pdata->server);
		dbloginfree(login);
		return (FALSE);
	}
	dbloginfree(login);
	login = NULL;

	return (TRUE);
}

int
do_rpc(RPCPARAMDATA * pdata, DBPROCESS * dbproc)
{
	printf("TODO: %s, ver %d\n", pdata->spname, dbtds(dbproc));
/* TODO...
	DBINT rows_read = 0;
	int i;
	int num_cols = 0;
	RETCODE ret_code = 0;

	if (FAIL == dbrpcinit(dbproc, pdata->spname))
		return FALSE;

	//dbrpcparam...

	if (dbrpcsend(dbproc) == FAIL) {
		fprintf(stderr, "dbsqlexec failed\n");
		return FALSE;
	}

	while (NO_MORE_RESULTS != (ret_code = dbresults(dbproc))) {
		if (ret_code == SUCCEED && num_cols == 0) {
			num_cols = dbnumcols(dbproc);
		}
	}

	if (0 == num_cols) {
		fprintf(stderr, "Error in dbnumcols\n");
		return FALSE;
	}

	for (i = 1; i < num_cols; ++i) {
		if (bcp_colfmt(dbproc, i, SYBCHAR, 0, -1, (const BYTE *) pdata->fieldterm,
			       pdata->fieldtermlen, i) == FAIL) {
			fprintf(stderr, "Error in bcp_colfmt col %d\n", i);
			return FALSE;
		}
	}

	if (bcp_colfmt(dbproc, num_cols, SYBCHAR, 0, -1, (const BYTE *) pdata->rowterm,
		       pdata->rowtermlen, num_cols) == FAIL) {
		fprintf(stderr, "Error in bcp_colfmt col %d\n", num_cols);
		return FALSE;
	}
*/

	return TRUE;
}

int
setoptions(DBPROCESS * dbproc, RPCPARAMDATA * params)
{
	RETCODE fOK;

	if (dbfcmd(dbproc, "set textsize %d ", params->textsize) == FAIL) {
		fprintf(stderr, "setoptions() could not set textsize at %s:%d\n", __FILE__, __LINE__);
		return FALSE;
	}

	/*
	 * If the option is a filename, read the SQL text from the file.
	 * Else pass the option verbatim to the server.
	 */
	if (params->options) {
		FILE *optFile;
		char optBuf[256];

		if ((optFile = fopen(params->options, "r")) == NULL) {
			if (dbcmd(dbproc, params->options) == FAIL) {
				fprintf(stderr, "setoptions() failed preparing options at %s:%d\n", __FILE__, __LINE__);
				return FALSE;
			}
		} else {
			while (fgets (optBuf, sizeof(optBuf), optFile) != NULL) {
				if (dbcmd(dbproc, optBuf) == FAIL) {
					fprintf(stderr, "setoptions() failed preparing options at %s:%d\n", __FILE__, __LINE__);
					fclose(optFile);
					return FALSE;
				}
			}
			if (!feof (optFile)) {
				perror("freerpc");
				fprintf(stderr, "error reading options file \"%s\" at %s:%d\n", params->options, __FILE__, __LINE__);
				fclose(optFile);
				return FALSE;
			}
			fclose(optFile);
		}
	}

	if (dbsqlexec(dbproc) == FAIL) {
		fprintf(stderr, "setoptions() failed sending options at %s:%d\n", __FILE__, __LINE__);
		return FALSE;
	}

	while ((fOK = dbresults(dbproc)) == SUCCEED) {
		while ((fOK = dbnextrow(dbproc)) == REG_ROW)
			continue;
		if (fOK == FAIL) {
			fprintf(stderr, "setoptions() failed sending options at %s:%d\n", __FILE__, __LINE__);
			return FALSE;
		}
	}
	if (fOK == FAIL) {
		fprintf(stderr, "setoptions() failed sending options at %s:%d\n", __FILE__, __LINE__);
		return FALSE;
	}

	return TRUE;
}

void
pusage(void)
{
	fprintf(stderr, "usage:  freerpc procedure [param1..paramN]\n");
	fprintf(stderr, "        [-U username] [-P password] [-I interfaces_file] [-S server] [-D database]\n");
	fprintf(stderr, "        [-v] [-d] [-O \"set connection_option on|off, ...]\"\n");
	fprintf(stderr, "        [-A packet size] [-T text or image size]\n");
	fprintf(stderr, "        \n");
	fprintf(stderr, "example: freerpc sp_help sp_help -S mssql -U guest -P password\n");
}

int
err_handler(DBPROCESS * dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr)
{
	if (dberr) {
		fprintf(stderr, "Msg %d, Level %d\n", dberr, severity);
		fprintf(stderr, "%s\n\n", dberrstr);
	}

	else {
		fprintf(stderr, "DB-LIBRARY error:\n\t");
		fprintf(stderr, "%s\n", dberrstr);
	}

	return INT_CANCEL;
}

int
msg_handler(DBPROCESS * dbproc, DBINT msgno, int msgstate, int severity, char *msgtext, char *srvname, char *procname, int line)
{
	/*
	 * If it's a database change message, we'll ignore it.
	 * Also ignore language change message.
	 */
	if (msgno == 5701 || msgno == 5703)
		return (0);

	fprintf(stderr, "Msg %ld, Level %d, State %d\n", (long) msgno, severity, msgstate);

	if (strlen(srvname) > 0)
		fprintf(stderr, "Server '%s', ", srvname);
	if (strlen(procname) > 0)
		fprintf(stderr, "Procedure '%s', ", procname);
	if (line > 0)
		fprintf(stderr, "Line %d", line);

	fprintf(stderr, "\n\t%s\n", msgtext);

	return (0);
}
