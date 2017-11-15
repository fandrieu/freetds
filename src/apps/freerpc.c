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
enum datatype get_datatype(int type);
int process_parameter_rpctype(RPCPARAMDATA * pdata, char *optarg);
int process_parameter_rpcprm(RPCPARAMDATA * pdata, char *optarg);
int free_parameters(RPCPARAMDATA * pdata);
int login_to_database(struct pd *, DBPROCESS **);

int print_input_debug(RPCPARAMDATA * pdata, DBPROCESS * dbproc);
int print_columns(RPCPARAMDATA * pdata, DBPROCESS * dbproc);
int print_column_or_return(RPCPARAMDATA * pdata, DBPROCESS * dbproc, int colno, int is_ret, char *to_file);
int print_row(RPCPARAMDATA * pdata, DBPROCESS * dbproc, int num_cols);
int print_returns(RPCPARAMDATA * pdata, DBPROCESS * dbproc);
int do_rpc(RPCPARAMDATA * pdata, DBPROCESS * dbproc);
int setoptions (DBPROCESS * dbproc, RPCPARAMDATA * params);

int err_handler(DBPROCESS * dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr);
int msg_handler(DBPROCESS * dbproc, DBINT msgno, int msgstate, int severity, char *msgtext, char *srvname, char *procname,
		int line);

int read_file(char *name, BYTE **pbuf, int *plen, int maxlen);
int write_file(char *name, char *buf, long len);
char *to_uppercase(char *s);
#define PRINT_NAME_OR_NUM(name, num) name && strlen(name) ? printf("%s", name) : printf("[%d]", num)

int
main(int argc, char **argv)
{
	RPCPARAMDATA params;
	DBPROCESS *dbproc;
	int ok = 0;

	setlocale(LC_ALL, "");

#ifdef __VMS
	/* Convert VMS-style arguments to Unix-style */
	parse_vms_args(&argc, &argv);
#endif

	memset(&params, '\0', sizeof(params));

	params.textsize = 2147483647;	/* our default text size is the LARGEST */

	if (process_parameters(argc, argv, &params) == FALSE) {
		goto cleanup;
	}
	if (getenv("FREERPC")) {
		fprintf(stderr, "User name: \"%s\"\n", params.user);
	}


	if (login_to_database(&params, &dbproc) == FALSE) {
		goto cleanup;
	}

	if (setoptions(dbproc, &params) == FALSE) {
		goto cleanup;
	}

	ok = do_rpc(&params, dbproc);

cleanup:
	free_parameters(&params);
	exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);

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
	while ((ch = getopt(argc, argv, "p:n:ft:oNU:P:I:S:T:A:V:O:0:C:dvD:")) != -1) {
		switch (ch) {
		case 'v':
			pdata->verbose++;
			break;
		case 'd':
			tdsdump_open(NULL);
			break;
		case 'p':
			if (!process_parameter_rpcprm(pdata, optarg))
				return FALSE;
			break;
		case 't':
			if (!process_parameter_rpctype(pdata, optarg))
				return FALSE;
			break;
		case 'N':
			pdata->paramnull = 1;
			break;
		case 'n':
			free(pdata->paramname);
			pdata->paramname = strdup(optarg);
			break;
		case 'f':
			pdata->paramfile = 1;
			break;
		case 'o':
			pdata->paramoutput = 1;
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
		case 'V':
			pdata->version = strdup(optarg);
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

/* NOTE: don't use FreeTDS internal macros on purpose */
enum datatype
get_datatype(int type) {
	switch(type) {
		case SYBINT1:
		case SYBINT2:
		case SYBINT4:
		case SYBINT8:
		case SYBBIT:
		case SYBMONEY4:
		case SYBMONEY:
			return DATATYPE_LONG;
		case SYBFLT8:
		case SYBREAL:
		case SYBNUMERIC:
		case SYBDECIMAL:
			return DATATYPE_DOUBLE;
		default:
			return DATATYPE_STR;
	}
}

int
process_parameter_rpctype(RPCPARAMDATA * pdata, char *optarg)
{
	char *s = to_uppercase(optarg);
	int type = 0, i;

	/* enable all for testing purposes... */
	const RPCKEYVAL types[] = {
		{"DEFAULT", RPCRPM_DEFAULTTYPE},
		{"0", RPCRPM_DEFAULTTYPE},
		{"CHAR", SYBCHAR},
		{"VARCHAR", SYBVARCHAR},
		/* {"INTN", SYBINTN},	needs len */
		{"INT1", SYBINT1},
		{"INT2", SYBINT2},
		{"INT4", SYBINT4},
		{"INT8", SYBINT8},
		{"FLT8", SYBFLT8},
		{"DATETIME", SYBDATETIME},
		{"BIT", SYBBIT},
		/* {"BITN", SYBBITN},	needs len */
		{"TEXT", SYBTEXT},
		{"NTEXT", SYBNTEXT},
		{"IMAGE", SYBIMAGE},
		{"MONEY4", SYBMONEY4},
		{"MONEY", SYBMONEY},
		{"DATETIME4", SYBDATETIME4},
		{"REAL", SYBREAL},
		{"BINARY", SYBBINARY},
		/* {"VOID", SYBVOID},	segfault */
		{"VARBINARY", SYBVARBINARY},
		{"NUMERIC", SYBNUMERIC},
		{"DECIMAL", SYBDECIMAL},
		/* {"FLTN", SYBFLTN},	needs len */
		/* {"MONEYN", SYBMONEYN},	needs len */
		/* {"DATETIMN", SYBDATETIMN},	needs len */
		{"NVARCHAR", SYBNVARCHAR},
		{"DATE", SYBDATE},
		{"TIME", SYBTIME},
		{"BIGDATETIME", SYB5BIGDATETIME},
		{"BIGTIME", SYB5BIGTIME},
		{"MSDATE", SYBMSDATE},
		{"MSTIME", SYBMSTIME},
		{"MSDATETIME2", SYBMSDATETIME2},
		{"MSDATETIMEOFFSET", SYBMSDATETIMEOFFSET},
	};
	int typeslen = sizeof(types)/sizeof(types[0]);

	for(i=0; i<typeslen; i++) {
		if(strcmp(s, types[i].key) == 0) {
			type = types[i].value;
			break;
		}
	}
	free(s);
	if (!type) {
		fprintf(stderr, "Unsupported rpc param type %s\n", optarg);
		pdata->paramtype = RPCRPM_DEFAULTTYPE;
		return FALSE;
	}

	pdata->paramtype = type;
	return TRUE;
}

int
process_parameter_rpcprm(RPCPARAMDATA * pdata, char *optarg)
{
	RPCPRMPARAMDATA *rpcprm;

	if (!pdata->paramslen) {
		pdata->paramslen = 1;
		pdata->params = malloc(sizeof(RPCPRMPARAMDATA*));
	} else {
		pdata->paramslen++;
		pdata->params = realloc(pdata->params, pdata->paramslen * sizeof(RPCPRMPARAMDATA*));
	}

	rpcprm = malloc(sizeof(RPCPRMPARAMDATA));
	memset(rpcprm, 0, sizeof(*rpcprm));
	pdata->params[pdata->paramslen-1] = rpcprm;

	rpcprm->type = pdata->paramtype ? pdata->paramtype : RPCRPM_DEFAULTTYPE;
	if (pdata->paramname) {
		rpcprm->name = pdata->paramname;
		pdata->paramname = NULL;
	}
	if (pdata->paramoutput) {
		rpcprm->output = pdata->paramoutput;
		pdata->paramoutput = 0;
	}
	if (pdata->paramfile) {
		rpcprm->file = strdup(optarg);
		pdata->paramfile = 0;
	}
	if (pdata->paramnull) {
		pdata->paramnull = 0;

		return TRUE;
	}

	/* read raw data from file */
	if (rpcprm->file) {
		return read_file(rpcprm->file, &(rpcprm->value), &(rpcprm->strlen), pdata->textsize);
	}

	/* convert non-text data */
	/* TODO: parse date ... */
	switch(get_datatype(rpcprm->type)) {
		case DATATYPE_LONG: {
			int64_t *tmp;
			double mny;
			tmp = malloc(sizeof(*tmp));
			if (rpcprm->type == SYBMONEY || rpcprm->type == SYBMONEY4) {
				mny = atof(optarg);
				*tmp = (int64_t)(mny * 10000);
			} else {
#ifdef WIN32
				*tmp = _atoi64(optarg);
#else
				*tmp = atoll(optarg);
#endif
			}
			rpcprm->value = (BYTE *)tmp;
			break;
		}
		case DATATYPE_DOUBLE: {
			double *tmp;
			tmp = malloc(sizeof(*tmp));
			*tmp = atof(optarg);
			rpcprm->value = (BYTE *)tmp;
			break;
		}
		case DATATYPE_STR: {
			rpcprm->value = (BYTE *)(strdup(optarg));
			rpcprm->strlen = (int)strlen(optarg);
			break;
		}
		default:
			fprintf(stderr, "Internal error: missing datatype %d\n", get_datatype(rpcprm->type));
	}

	return TRUE;
}

int
free_parameters(RPCPARAMDATA * pdata)
{
	int i;

	if (!pdata->paramslen) {
		return TRUE;
	}

	for (i=0; i<pdata->paramslen; i++) {
		RPCPRMPARAMDATA *rpcprm;
		rpcprm = pdata->params[i];
		if (rpcprm->value)
			free(rpcprm->value);
		if (rpcprm->file)
			free(rpcprm->file);
		if (rpcprm->name)
			free(rpcprm->name);
		free(rpcprm);
	}

	free(pdata->params);
	pdata->paramslen = 0;
	return TRUE;
}

int
login_to_database(RPCPARAMDATA * pdata, DBPROCESS ** pdbproc)
{
	LOGINREC *login;

	const RPCKEYVAL versions[] = {
		{"4.2", DBVERSION_42},
		{"4.6", DBVERSION_46},
		{"7.0", DBVERSION_70},
		{"7.1", DBVERSION_71},
		{"7.2", DBVERSION_72},
		{"7.3", DBVERSION_73},
		{"7.4", DBVERSION_74},
		{"10.0", DBVERSION_100},
		{"auto", 0}
	};
	int versionslen = sizeof(versions)/sizeof(versions[0]);
	int i;

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

	if (pdata->version) {
		for(i=0; i<versionslen; i++) {
			if(strcmp(pdata->version, versions[i].key) == 0) {
				DBSETLVERSION(login, versions[i].value);
				break;
			}
		}
	}
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
print_input_debug(RPCPARAMDATA * pdata, DBPROCESS * dbproc)
{
	int i;
	RPCPRMPARAMDATA *rpcprm;

	const RPCKEYVAL versions[] = {
		{"UNKNOWN", DBTDS_UNKNOWN},
		{"2.0", DBTDS_2_0},
		{"3.4", DBTDS_3_4},
		{"4.0", DBTDS_4_0},
		{"4.2", DBTDS_4_2},
		{"4.6", DBTDS_4_6},
		{"4.9", DBTDS_4_9_5},
		{"5.0", DBTDS_5_0},
		{"7.0", DBTDS_7_0},
		{"7.1", DBTDS_7_1},
		{"7.2", DBTDS_7_2},
		{"7.3", DBTDS_7_3},
		{"7.4", DBTDS_7_4}
	};
	int versionslen = sizeof(versions)/sizeof(versions[0]);
	int verint;
	const char *vertext = "ERROR";

	fprintf(stderr, "exec %s", pdata->spname);
	if (!pdata->paramslen) {
		fprintf(stderr, "\n");
		return TRUE;
	}

	fprintf(stderr, ", params:\n");
	for (i=0; i<pdata->paramslen; i++) {
		rpcprm = pdata->params[i];
		fprintf(
			stderr,
			"%3d %s: type x%x, out %d, value ",
			i,
			rpcprm->name,
			rpcprm->type,
			rpcprm->output
		);
		if (rpcprm->value == NULL) {
			fprintf(stderr, "(null)\n");
			continue;
		}
		switch(get_datatype(rpcprm->type)) {
			case DATATYPE_LONG:
				fprintf(stderr, "(long) %lld\n", *((long long *)rpcprm->value));
				break;
			case DATATYPE_DOUBLE:
				fprintf(stderr, "(double) %f\n", *((double *)rpcprm->value));
				break;
			default:
				if (rpcprm->file) {
					fprintf(stderr, "(file) %d %s\n", rpcprm->strlen, rpcprm->file);
				} else {
					fprintf(stderr, "(char*) %d %s\n", rpcprm->strlen, (char *)rpcprm->value);
				}
		}
	}

	verint = dbtds(dbproc);
	for(i=0; i<versionslen; i++) {
		if(verint == versions[i].value) {
			vertext = versions[i].key;
		}
	}

	fprintf(stderr, "tds version %s (%d), %s\n", vertext, verint, TDS_VERSION_NO);

	return TRUE;
}

int
print_columns(RPCPARAMDATA * pdata, DBPROCESS * dbproc)
{
	int num_cols = dbnumcols(dbproc);
	int i;

	for (i=1; i<=num_cols; i++) {
		PRINT_NAME_OR_NUM((char *)dbcolname(dbproc, i), i);
		printf(i == num_cols ? "\n" : "\t");
	}

	return num_cols;
}

int
print_column_or_return(RPCPARAMDATA * pdata, DBPROCESS * dbproc, int colno, int is_ret, char *to_file)
{
	int type;
	BYTE *data;
	DBCHAR *tmp_data;
	DBINT data_len, tmp_data_len;

	if (is_ret) {
		type = dbrettype(dbproc, colno);
		data = dbretdata(dbproc, colno);
		data_len = dbretlen(dbproc, colno);
	} else {
		type = dbcoltype(dbproc, colno);
		data = dbdata(dbproc, colno);
		data_len = dbdatlen(dbproc, colno);
	}

	if (to_file) {
		/* write raw data */
		printf("(file) %d %s", data_len, to_file);
		return write_file(to_file, (char *)data, data_len);
	}

	if (!dbwillconvert(type, SYBCHAR)) {
		fprintf(stderr, "can't print column %d\n", colno);
		return FALSE;
	}

	tmp_data_len = 48 + (2 * (data_len)); /* FIXME: We allocate more than we need here */
	tmp_data = malloc(tmp_data_len);
	data_len = dbconvert(NULL, type, data, data_len, SYBCHAR, (BYTE*)tmp_data, -1);

	printf("%s", tmp_data);

	free(tmp_data);

	return TRUE;
}

int
print_row(RPCPARAMDATA * pdata, DBPROCESS * dbproc, int num_cols)
{
	int i;

	for (i=1; i<=num_cols; i++) {
		print_column_or_return(pdata, dbproc, i, 0, NULL);
		printf(i == num_cols ? "\n" : "\t");
	}

	return num_cols;
}

int
print_returns(RPCPARAMDATA * pdata, DBPROCESS * dbproc)
{
	RPCPRMPARAMDATA *rpcprm = NULL;
	int prm_i = -1, i;
	int num_rets = dbnumrets(dbproc);

	for (i=1; i<=num_rets; i++) {
		for (prm_i++; prm_i<pdata->paramslen; prm_i++) {
			rpcprm = pdata->params[prm_i];
			if (rpcprm->output)
				break;
		}
		if (!rpcprm) {
			fprintf(stderr, "Internal error: can't find output parameter n°%d\n", i);
			return FALSE;
		}
		PRINT_NAME_OR_NUM(rpcprm->name, prm_i);
		printf(": ");
		print_column_or_return(pdata, dbproc, i, 1, rpcprm->file);
		printf("\n");
	}

	return num_rets;
}

int
do_rpc(RPCPARAMDATA * pdata, DBPROCESS * dbproc)
{
	RETCODE ret_code = 0;
	int num_res = 0;
	int num_cols = 0;

	RPCPRMPARAMDATA *rpcprm;
	int i, datalen, status;
	long maxlen;

	if (pdata->verbose) {
		print_input_debug(pdata, dbproc);
	}

	/* rpc init: ->spname */
	if (FAIL == dbrpcinit(dbproc, pdata->spname, 0)) {
		fprintf(stderr, "dbrpcinit failed\n");
		return FALSE;
	}

	/* rpc bind: ->params */
	for (i=0; i<pdata->paramslen; i++) {
		rpcprm = pdata->params[i];
		maxlen = datalen = -1;
		status = rpcprm->output ? 1 : 0;
		if (DATATYPE_STR == get_datatype(rpcprm->type)) {
			datalen = rpcprm->strlen;
			if (rpcprm->output) {
				maxlen = 8000;
			}
		}
		if (rpcprm->value == NULL) {
			datalen = 0;
		}
		if (FAIL == dbrpcparam(dbproc, rpcprm->name, (BYTE)status, rpcprm->type, maxlen, datalen, rpcprm->value)) {
			fprintf(stderr, "dbrpcparam failed for %d / %s\n", i, rpcprm->name);
			/* NOTE: need to reset to retry: dbrpcinit(dbproc, "", DBRPCRESET);*/
			return FALSE;
		}
	}

	/* rpc send */
	if (dbrpcsend(dbproc) == FAIL) {
		fprintf(stderr, "dbrpcsend failed\n");
		return FALSE;
	}

	/* here we could clear the values
	free_parameters_values(pdata);
	*/

	/* fetch & print results */
	while (SUCCEED == (ret_code = dbresults(dbproc))) {
		printf("--------------------\nresult %d\n", ++num_res);
		num_cols = print_columns(pdata, dbproc);
		while(NO_MORE_ROWS != dbnextrow(dbproc)) {
			print_row(pdata, dbproc, num_cols);
		};
	}
	if (ret_code != SUCCEED && ret_code != NO_MORE_RESULTS) {
		fprintf(stderr, "dbresults failed\n");
		return FALSE;
	}
	printf("--------------------\nreturns\n");
	print_returns(pdata, dbproc);
	printf("status: %d\n", dbretstatus(dbproc));

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
	fprintf(stderr, "usage:  freerpc procedure\n");
	fprintf(stderr, "        [-n name] [-t type] [-o output_len] [-N] [-p param1]\n");
	fprintf(stderr, "        [-n name] [-p paramN]\n");
	fprintf(stderr, "        [-U username] [-P password] [-I interfaces_file] [-S server] [-D database]\n");
	fprintf(stderr, "        [-v] [-d] [-O \"set connection_option on|off, ...]\"\n");
	fprintf(stderr, "        [-A packet size] [-T text or image size]\n");
	fprintf(stderr, "        \n");
	fprintf(stderr, "example: freerpc sp_help -p sp_help -S mssql -U guest -P password\n");
	fprintf(stderr, "         freerpc sp_executesql -S mssql \\\n");
	fprintf(stderr, "         -t ntext -p \"select @a\" -p \"@a char(4)\" \\\n");
	fprintf(stderr, "         -n @a -t char -p test\n");
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

int
read_file(char *name, BYTE **pbuf, int *plen, int maxlen)
{
	FILE *fp;
	long len;
	size_t ok;
	char *buf;
	fp = fopen(name, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open input param file: %s\n", name);
		return FALSE;
	}
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	/* TODO: check fixed length... */
	if (len > maxlen) {
		fprintf(stderr, "input file truncated to %d\n", maxlen);
		len = maxlen;
	}

	buf = malloc(len + 1);
	ok = len ? fread(buf, len, 1, fp) : 1;
	fclose(fp);

	buf[len] = 0;

	if (!ok) {
		fprintf(stderr, "Error reading file %s\n", name);
	}

	*pbuf = (BYTE *)buf;
	*plen = len;

	return TRUE;
}

int
write_file(char *name, char *buf, long len)
{
	FILE *fp;
	size_t ok;

	fp = fopen(name, "wb");
	if (!fp) {
		fprintf(stderr, "Can't open output param file: %s\n", name);
		return FALSE;
	}
	ok = len ? fwrite(buf, len, 1, fp) : 1;
	fclose(fp);

	if (!ok) {
		fprintf(stderr, "Error writing file %s\n", name);
	}

	return TRUE;
}

char *
to_uppercase(char *s)
{
	int i = 0;
	char *str = strdup(s);

	while (str[i]) {
		if (str[i] >= 97 && str[i] <= 122)
			str[i] -= 32;
		i++;
	}
	return (str);
}
