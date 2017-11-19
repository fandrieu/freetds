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

#include "replacements.h"
#include <sybfront.h>
#include <sybdb.h>
#include "freerpc.h"

static int process_parameters(int argc, char **argv, RPCDATA *data, RPCLOGIN *login, RPCPARAMOPTS *paramopts);
static enum rpc_datatype get_datatype(int type);
static int is_fixed_length(int type);
#define IS_TEXT(type) get_datatype(type) == DATATYPE_STR
static void process_parameter_rpcname(RPCPARAMOPTS *paramopts, char *optarg);
static int process_parameter_rpctype(RPCPARAMOPTS *paramopts, char *optarg);
static int process_parameter_rpcparam(RPCDATA *data, RPCPARAMOPTS *paramopts, char *optarg);
static int init_dblib(void);
static int login_to_database(DBPROCESS **pdbproc, RPCLOGIN *login);
static int set_login_options(DBPROCESS *dbproc, char *options, int textsize);
static int sql_exec_skip(DBPROCESS *pdbproc);
static void print_usage(void);
static int print_input_debug(DBPROCESS *dbproc, RPCDATA *data);

static int fprint_columns(FILE *fpout, DBPROCESS *dbproc);
static int fprint_column_or_return(FILE *fpout, DBPROCESS *dbproc, int colno, int is_ret, char *to_file);
static int fprint_row(FILE *fpout, DBPROCESS *dbproc, int num_cols);
static int fprint_returns(FILE *fpout, DBPROCESS * dbproc, RPCDATA *data);
static int rpc_main(DBPROCESS * dbproc, RPCDATA *data);

static int err_handler(DBPROCESS *dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr);
static int msg_handler(DBPROCESS *dbproc, DBINT msgno, int msgstate, int severity, char *msgtext, char *srvname,
	char *procname, int line);

static void *xmalloc(size_t s);
static void *xcalloc(size_t s);
static void *xrealloc(void *p, size_t s);
static void free_paramopts(RPCPARAMOPTS *paramopts);
static void free_login(RPCLOGIN *login);
static void free_data_param(RPCPARAM *param);
static void free_data(RPCDATA *data);
static void free_data_paramsvalue(RPCDATA *data);

static char *to_lowercase(char *s);
static int read_file(char *name, BYTE **pbuf, size_t *plen, int is_text);
static int write_file(char *name, BYTE *buf, size_t len);
#define FPRINT_NAME_OR_NUM(f, name, num) name && strlen(name) ? fprintf(f, "%s", name) : fprintf(f, "[%d]", num)

int
main(int argc, char **argv)
{
	DBPROCESS *dbproc = NULL;
	RPCDATA *data = xcalloc(sizeof(RPCDATA));
	RPCLOGIN *login = xcalloc(sizeof(RPCLOGIN));
	RPCPARAMOPTS *paramopts = xcalloc(sizeof(RPCPARAMOPTS));
	int ok = 0;

	setlocale(LC_ALL, "");

	init_dblib();	/* need to process params with dbconvert */
	data->fpout = stdout;
	if (process_parameters(argc, argv, data, login, paramopts) == FALSE) {
		goto cleanup;
	}

	if (login_to_database(&dbproc, login) == FALSE) {
		goto cleanup;
	}
	if (set_login_options(dbproc, login->options, login->textsize) == FALSE) {
		goto cleanup;
	}

	
	ok = rpc_main(dbproc, data);

cleanup:
	if (dbproc) {
		dbclose(dbproc);
	}
	free_paramopts(paramopts);
	free_login(login);
	free_data(data);
	exit(ok ? data->status : EXIT_FAILURE);

	return 0;
}

static int
process_parameters(int argc, char **argv, RPCDATA *data,
	RPCLOGIN *login, RPCPARAMOPTS *paramopts)
{
	extern char *optarg;
	extern int optind;
	extern int optopt;

	int ch;
	if (argc < 2) {
		print_usage();
		return (FALSE);
	}

	/* argument 1 - the stored procedure name */
	optind = 2; /* start processing options after spname */
	data->spname = strdup(argv[1]);
	if (data->spname == NULL) {
		fprintf(stderr, "Out of memory!\n");
		return FALSE;
	}
	/* "exec" shortcut: calls sp_executesql with the next two args */
	if (strcmp(data->spname, "exec") == 0) {
		if (argc < 4) {
			print_usage();
			return (FALSE);
		}
		optind += 2;
		free(data->spname);
		data->spname = strdup("sp_executesql");
		process_parameter_rpctype(paramopts, "ntext");
		process_parameter_rpcparam(data, paramopts, argv[2]);
		process_parameter_rpcparam(data, paramopts, argv[3]);
		process_parameter_rpctype(paramopts, "default");
	}

	/*
	 * Get the rest of the arguments
	 */
	while ((ch = getopt(argc, argv, "p:n:ft:oNU:P:I:S:T:A:V:O:0:C:vD:")) != -1) {
		switch (ch) {
		/* global */
		case 'v':
			data->verbose++;
			break;
		/* rpc params */
		case 'p':
			if (!process_parameter_rpcparam(data, paramopts, optarg))
				return FALSE;
			break;
		case 't':
			if (!process_parameter_rpctype(paramopts, optarg))
				return FALSE;
			break;
		case 'N':
			paramopts->null = 1;
			break;
		case 'n':
			process_parameter_rpcname(paramopts, optarg);
			break;
		case 'f':
			paramopts->file = 1;
			break;
		case 'o':
			paramopts->output = 1;
			break;
		/* login */
		case 'U':
			login->user = strdup(optarg);
			break;
		case 'P':
			login->pass = strdup(optarg);
			break;
		case 'I':
			free(login->interfacesfile);
			login->interfacesfile = strdup(optarg);
			break;
		case 'S':
			login->server = strdup(optarg);
			break;
		case 'D':
			login->dbname = strdup(optarg);
			break;
		case 'O':
		case '0':
			login->options = strdup(optarg);
			break;
		case 'A':
			login->packetsize = atoi(optarg);
			break;
		case 'T':
			login->textsize = atoi(optarg);
			break;
		case 'C':
			login->charset = strdup(optarg);
			break;
		case 'V':
			login->version = strdup(optarg);
			break;
		case '?':
		default:
			print_usage();
			return (FALSE);
		}
	}

	/*
	 * Check for required/disallowed option combinations
	 * If no username is provided, rely on domain login.
	 */
	if (!login->server) {
		if ((login->server = getenv("DSQUERY")) != NULL) {
			login->server = strdup(login->server);	/* can be freed */
		} else {
			fprintf(stderr, "-S must be supplied.\n");
			return (FALSE);
		}
	}
	if (login->user && !(login->pass && strcmp(login->pass, "-"))) {
		login->pass = xmalloc(128);
		readpassphrase("Password: ", login->pass, 128, RPP_ECHO_OFF);
	}

	return (TRUE);
}

/* NOTE: don't use FreeTDS internal macros on purpose */
static enum rpc_datatype
get_datatype(int type) {
	switch(type) {
		case SYBCHAR:
		case SYBVARCHAR:
		case SYBNVARCHAR:
		case SYBTEXT:
		case SYBNTEXT:
		case SYBBINARY:
		case SYBVARBINARY:
		case SYBIMAGE:
			return DATATYPE_STR;
		case SYBINT1:
		case SYBINT2:
		case SYBINT4:
		case SYBINT8:
		case SYBINTN:
		case SYBBIT:
		case SYBBITN:
			return DATATYPE_LONG;
		case SYBFLT8:
		case SYBFLTN:
		case SYBREAL:
			return DATATYPE_DOUBLE;
		default:
			return DATATYPE_BYTES;
	}
}

static int
is_fixed_length(int type)
{
	switch(type) {
		case SYBINT1:
		case SYBINT2:
		case SYBINT4:
		case SYBINT8:
		case SYBBIT:
		case SYBREAL:
		case SYBFLT8:
		case SYBMONEY4:
		case SYBMONEY:
		case SYBDATETIME4:
		case SYBDATETIME:
		case SYBDATE:
		case SYBTIME:
			return 1;
		default:
			return 0;
	}
}

static void
process_parameter_rpcname(RPCPARAMOPTS *paramopts, char *optarg)
{
	DBINT len;

	free(paramopts->name);

	if (optarg[0] == '@') {
		paramopts->name = strdup(optarg);
		return;
	}

	len = strlen(optarg);
	paramopts->name = xmalloc(len+2);
	paramopts->name[0] = '@';
	memcpy(paramopts->name + 1, optarg, len + 1);
}

static int
process_parameter_rpctype(RPCPARAMOPTS *paramopts, char *optarg)
{
	char *s = to_lowercase(optarg);
	int type = 0, i;
	const RPCKEYVAL types[] = {
		{"default", RPCPARAM_DEFAULTTYPE},
		{"0", RPCPARAM_DEFAULTTYPE},
		{"char", SYBCHAR},
		{"varchar", SYBVARCHAR},
		{"nvarchar", SYBNVARCHAR},
		{"text", SYBTEXT},
		{"ntext", SYBNTEXT},
		/* {"void", SYBVOID},	assert column_varint_size */
		{"binary", SYBBINARY},
		{"varbinary", SYBVARBINARY},
		{"image", SYBIMAGE},
		{"bit", SYBBIT},
		{"bitn", SYBBITN},
		{"int1", SYBINT1},
		{"int2", SYBINT2},
		{"int4", SYBINT4},
		{"int8", SYBINT8},
		{"intn", SYBINTN},
		{"money4", SYBMONEY4},
		{"money", SYBMONEY},
		{"moneyn", SYBMONEYN},
		{"real", SYBREAL},
		{"flt8", SYBFLT8},
		{"fltn", SYBFLTN},
		/* {"decimal", SYBDECIMAL}, FIXME */
		/* {"numeric", SYBNUMERIC}, FIXME */
		{"datetime4", SYBDATETIME4},
		{"datetime", SYBDATETIME},
		{"datetimn", SYBDATETIMN},
		{"date", SYBDATE},
		{"time", SYBTIME},
		{"bigdatetime", SYBBIGDATETIME},
		{"bigtime", SYBBIGTIME},
		{"msdate", SYBMSDATE},
		{"mstime", SYBMSTIME},
		{"msdatetime2", SYBMSDATETIME2},
		{"msdatetimeoffset", SYBMSDATETIMEOFFSET}
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
		fprintf(stderr, "Supported types:\n");
		for(i=0; i<typeslen; i++) {
			fprintf(stderr, "\t%-10s 0x%x\n", types[i].key, types[i].value);
		}
		fprintf(stderr, "Unsupported rpc param type %s\n", optarg);
		paramopts->type = RPCPARAM_DEFAULTTYPE;
		return FALSE;
	}

	paramopts->type = type;
	return TRUE;
}

static int
process_parameter_rpcparam(RPCDATA *data, RPCPARAMOPTS *paramopts, char *optarg)
{
	RPCPARAM *param;
	size_t valuelen;

	if (!data->paramslen) {
		data->paramslen = 1;
		data->params = xmalloc(sizeof(RPCPARAM*));
	} else {
		data->paramslen++;
		data->params = xrealloc(data->params, data->paramslen * sizeof(RPCPARAM*));
	}

	param = xcalloc(sizeof(RPCPARAM));
	data->params[data->paramslen-1] = param;

	param->type = paramopts->type ? paramopts->type : RPCPARAM_DEFAULTTYPE;
	if (paramopts->name) {
		param->name = paramopts->name;
		paramopts->name = NULL;
	}
	if (paramopts->output) {
		param->output = paramopts->output;
		paramopts->output = 0;
	}
	if (paramopts->file) {
		param->file = strdup(optarg);
		paramopts->file = 0;
		if (!strcmp("-", param->file)) {
			if (!paramopts->null && paramopts->stdincheck++) {
				fprintf(stderr, "Param %d: stdin can only be selected once\n", data->paramslen);
				return FALSE;
			}
			/* redirect if outputing to stdout */
			if (param->output) {
				data->fpout = stderr;
			}
		}
	}
	if (paramopts->null) {
		paramopts->null = 0;
		return TRUE;
	}

	/* read raw data from file */
	if (param->file) {
		return read_file(param->file, &(param->value), (size_t *)&(param->valuelen), IS_TEXT(param->type));
	}

	/* set text data as is */
	if (IS_TEXT(param->type)) {
		param->value = (BYTE *)(strdup(optarg));
		param->valuelen = (DBINT)strlen(optarg);
		return TRUE;
	}

	/* convert non-text data */
	if (!dbwillconvert(SYBCHAR, param->type)) {
		fprintf(stderr, "Param %d: can't convert type x%x\n", data->paramslen, param->type);
		return FALSE;
	}

	valuelen = MAX_BYTESIZE;
	param->value = xcalloc(valuelen);
	param->valuelen = dbconvert(NULL, SYBCHAR, (BYTE *)optarg, (int)strlen(optarg),
		param->type, param->value, valuelen);

	if (param->valuelen < 0) {
		fprintf(stderr, "Param %d: error converting \"%s\" to type x%x\n", data->paramslen, optarg, param->type);
		return FALSE;
	}

	return TRUE;
}

static int
init_dblib(void)
{
	/* Initialize DB-Library. */

	if (dbinit() == FAIL)
		return FALSE;

	/*
	 * Install the user-supplied error-handling and message-handling
	 * routines. They are defined at the bottom of this source file.
	 */

	dberrhandle(err_handler);
	dbmsghandle(msg_handler);

	return TRUE;
}

static int
login_to_database(DBPROCESS **pdbproc, RPCLOGIN *login)
{
	LOGINREC *loginrec;

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

	/* If the interfaces file was specified explicitly, set it. */
	if (login->interfacesfile != NULL)
		dbsetifile(login->interfacesfile);

	/*
	 * Allocate and initialize the LOGINREC structure to be used
	 * to open a connection to SQL Server.
	 */

	loginrec = dblogin();
	if (!loginrec)
		return FALSE;

	if (login->user)
		DBSETLUSER(loginrec, login->user);
	if (login->pass) {
		DBSETLPWD(loginrec, login->pass);
		memset(login->pass, 0, strlen(login->pass));
	}

	DBSETLAPP(loginrec, "FreeRPC");
	if (login->charset)
		DBSETLCHARSET(loginrec, login->charset);

	if (login->packetsize && login->packetsize > 0)
		DBSETLPACKET(loginrec, login->packetsize);

	if (login->dbname)
		DBSETLDBNAME(loginrec, login->dbname);

	if (login->version) {
		for(i=0; i<versionslen; i++) {
			if(strcmp(login->version, versions[i].key) == 0) {
				DBSETLVERSION(loginrec, versions[i].value);
				break;
			}
		}
	}
	/*
	 * Get a connection to the database.
	 */

	if ((*pdbproc = dbopen(loginrec, login->server)) == NULL) {
		fprintf(stderr, "Can't connect to server \"%s\".\n", login->server);
		dbloginfree(loginrec);
		return (FALSE);
	}
	dbloginfree(loginrec);
	loginrec = NULL;

	return (TRUE);
}

static int
print_input_debug(DBPROCESS *dbproc, RPCDATA *data)
{
	int i, b;
	RPCPARAM *param;

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

	fprintf(stderr, "exec %s", data->spname);
	if (!data->paramslen) {
		fprintf(stderr, "\n");
		return TRUE;
	}

	fprintf(stderr, ", params:\n");
	for (i=0; i<data->paramslen; i++) {
		param = data->params[i];
		fprintf(stderr, "%3d %s: type x%x, out %d, len%5d ",
			i, param->name, param->type, param->output, param->valuelen);
		if (param->value == NULL) {
			fprintf(stderr, "(null)\n");
			continue;
		}
		switch(get_datatype(param->type)) {
			case DATATYPE_LONG:
				fprintf(stderr, "(int) %lld\n", *((long long *)param->value));
				break;
			case DATATYPE_DOUBLE:
				fprintf(stderr, "(float) %f\n", *((double *)param->value));
				break;
			case DATATYPE_BYTES:
				fprintf(stderr, "(bytes) ");
				for (b=0; b<param->valuelen; b++)
					fprintf(stderr, "%s%02x", b?":":"", param->value[b]);
				fprintf(stderr, "\n");
				break;
			default:
				if (param->file)
					fprintf(stderr, "(file) %s\n", param->file);
				else	/* FIXME: binary types */
					fprintf(stderr, "(string) %s\n", (char *)param->value);
		}
	}

	verint = dbtds(dbproc);
	for(i=0; i<versionslen; i++) {
		if(verint == versions[i].value)
			vertext = versions[i].key;
	}

	fprintf(stderr, "tds version %s (%d), %s\n", vertext, verint, dbversion());

	return TRUE;
}

static int
fprint_columns(FILE *fpout, DBPROCESS *dbproc)
{
	int num_cols = dbnumcols(dbproc);
	int i;

	for (i=1; i<=num_cols; i++) {
		FPRINT_NAME_OR_NUM(fpout, (char *)dbcolname(dbproc, i), i);
		fprintf(fpout, i == num_cols ? "\n" : "\t");
	}

	return num_cols;
}

static int
fprint_column_or_return(FILE *fpout, DBPROCESS *dbproc, int colno, int is_ret, char *to_file)
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
		fprintf(fpout, "(file) %d %s", data_len, to_file);
		return write_file(to_file, data, data_len);
	}

	if (!dbwillconvert(type, SYBCHAR)) {
		fprintf(stderr, "can't print column %d\n", colno);
		return FALSE;
	}

	tmp_data_len = 48 + (2 * (data_len)); /* FIXME: We allocate more than we need here */
	tmp_data = xmalloc(tmp_data_len);
	data_len = dbconvert(NULL, type, data, data_len, SYBCHAR, (BYTE*)tmp_data, -1);

	fprintf(fpout, "%s", tmp_data);

	free(tmp_data);

	return TRUE;
}

static int
fprint_row(FILE *fpout, DBPROCESS *dbproc, int num_cols)
{
	int i;

	for (i=1; i<=num_cols; i++) {
		fprint_column_or_return(fpout, dbproc, i, 0, NULL);
		fprintf(fpout, i == num_cols ? "\n" : "\t");
	}

	return num_cols;
}

static int
fprint_returns(FILE *fpout, DBPROCESS *dbproc, RPCDATA *data)
{
	RPCPARAM *param = NULL;
	int prm_i = -1, i;
	int num_rets = dbnumrets(dbproc);

	for (i=1; i<=num_rets; i++) {
		for (prm_i++; prm_i<data->paramslen; prm_i++) {
			param = data->params[prm_i];
			if (param->output)
				break;
		}
		if (!param) {
			fprintf(stderr, "Internal error: can't find output parameter n°%d\n", i);
			return FALSE;
		}
		FPRINT_NAME_OR_NUM(fpout, param->name, prm_i);
		fprintf(fpout, ": ");
		fprint_column_or_return(fpout, dbproc, i, 1, param->file);
		fprintf(fpout, "\n");
	}

	return num_rets;
}

static int
rpc_main(DBPROCESS *dbproc, RPCDATA *data)
{
	RETCODE ret_code = 0;
	int num_res = 0;
	int num_cols = 0;
	RPCPARAM *param;
	DBINT datalen, maxlen;
	int i, status;

	if (data->verbose) {
		print_input_debug(dbproc, data);
	}

	/* rpc init: ->spname */
	if (FAIL == dbrpcinit(dbproc, data->spname, 0)) {
		fprintf(stderr, "dbrpcinit failed\n");
		return FALSE;
	}

	/* rpc bind: ->params */
	for (i=0; i<data->paramslen; i++) {
		param = data->params[i];
		datalen = maxlen = -1;
		status = param->output ? DBRPCRETURN : 0;
		if (!is_fixed_length(param->type)) {
			datalen = param->valuelen;
			if (param->output) {
				/* blob types are limited by textsize, not by this */
				maxlen = 8000;
			}
		}
		if (param->value == NULL) {
			datalen = 0;
		}
		if (FAIL == dbrpcparam(dbproc, param->name, (BYTE)status, param->type, maxlen, datalen, param->value)) {
			fprintf(stderr, "dbrpcparam failed for %d / %s\n", i, param->name);
			/* NOTE: need to reset to retry: dbrpcinit(dbproc, "", DBRPCRESET);*/
			return FALSE;
		}
	}

	/* rpc send */
	if (dbrpcsend(dbproc) == FAIL) {
		fprintf(stderr, "dbrpcsend failed\n");
		return FALSE;
	}

	/* once sent we can free the params values */
	free_data_paramsvalue(data);

	/* fetch & print results */
	while ((ret_code = dbresults(dbproc)) == SUCCEED) {
		fprintf(data->fpout, "--------------------\nresult %d\n", ++num_res);
		num_cols = fprint_columns(data->fpout, dbproc);
		while(dbnextrow(dbproc) != NO_MORE_ROWS) {
			fprint_row(data->fpout, dbproc, num_cols);
		};
	}
	if (ret_code == FAIL) {
		fprintf(stderr, "dbresults failed\n");
		return FALSE;
	}
	if (dbhasretstat(dbproc)) {
		data->status = dbretstatus(dbproc);
	}
	fprintf(data->fpout, "--------------------\nreturns\n");
	fprint_returns(data->fpout, dbproc, data);
	fprintf(data->fpout, "status: %d\n", data->status);

	return TRUE;
}

static int
set_login_options(DBPROCESS *dbproc, char *options, int textsize)
{
	if (!textsize && !options) {
		return TRUE;
	}

	if (textsize && dbfcmd(dbproc, "set textsize %d ", textsize) == FAIL) {
		fprintf(stderr, "set_login_options() could not set textsize at %s:%d\n", __FILE__, __LINE__);
		return FALSE;
	}

	/*
	 * If the option is a filename, read the SQL text from the file.
	 * Else pass the option verbatim to the server.
	 */
	if (options) {
		FILE *optFile;
		char optBuf[256];

		if ((optFile = fopen(options, "r")) == NULL) {
			if (dbcmd(dbproc, options) == FAIL) {
				fprintf(stderr, "set_login_options() failed preparing options at %s:%d\n", __FILE__, __LINE__);
				return FALSE;
			}
		} else {
			while (fgets (optBuf, sizeof(optBuf), optFile) != NULL) {
				if (dbcmd(dbproc, optBuf) == FAIL) {
					fprintf(stderr, "set_login_options() failed preparing options at %s:%d\n", __FILE__, __LINE__);
					fclose(optFile);
					return FALSE;
				}
			}
			if (!feof (optFile)) {
				perror("freerpc");
				fprintf(stderr, "error reading options file \"%s\" at %s:%d\n", options, __FILE__, __LINE__);
				fclose(optFile);
				return FALSE;
			}
			fclose(optFile);
		}
	}

	if (!sql_exec_skip(dbproc)) {
		fprintf(stderr, "set_login_options() failed sending options at %s:%d\n", __FILE__, __LINE__);
		return FALSE;
	}

	return TRUE;
}

static int
sql_exec_skip(DBPROCESS *dbproc)
{
	int ret_code;

	if (dbsqlexec(dbproc) == FAIL) {
		return FALSE;
	}
	while ((ret_code = dbresults(dbproc)) == SUCCEED) {
		while(dbnextrow(dbproc) != NO_MORE_ROWS) {};
	}
	if (ret_code == FAIL) {
		return FALSE;
	}
	return TRUE;
}

static void
print_usage(void)
{
	fprintf(stderr, "usage: freerpc procedure_name\n");
	fprintf(stderr, "\t[-n param_name] [-t param_type] [-o] [-N] [-f] [-p param_value]\n");
	fprintf(stderr, "\t[-p param_valueN]\n");
	fprintf(stderr, "\t[-U username] [-P password] [-I interfaces_file] [-S server] [-D database]\n");
	fprintf(stderr, "\t[-v] [-O \"set connection_option on|off, ...]\"\n");
	fprintf(stderr, "\t[-A packet size] [-T textsize] [-V tds_version]\n");
}

static int
err_handler(DBPROCESS *dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr)
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

static int
msg_handler(DBPROCESS *dbproc, DBINT msgno, int msgstate, int severity, char *msgtext, char *srvname, char *procname, int line)
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

static void *
xmalloc(size_t s)
{
	void *p = malloc(s);

	if (!p) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	return p;
}

static void *
xcalloc(size_t s)
{
	void *p = xmalloc(s);
	memset(p, 0, s);
	return p;
}

static void *
xrealloc(void *p, size_t s)
{
	p = realloc(p, s);
	if (!p) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	return p;
}

static void
free_paramopts(RPCPARAMOPTS *paramopts)
{
	if (paramopts->name)
		free(paramopts->name);
	free(paramopts);
}

static void
free_login(RPCLOGIN *login)
{
	if (login->interfacesfile)
		free(login->interfacesfile);
	if (login->user)
		free(login->user);
	if (login->pass)
		free(login->pass);
	if (login->server)
		free(login->server);
	if (login->dbname)
		free(login->dbname);
	if (login->options)
		free(login->options);
	if (login->charset)
		free(login->charset);
	if (login->version)
		free(login->version);
	free(login);
}

static void
free_data_param(RPCPARAM *param)
{
	if (param->value)
		free(param->value);
	if (param->file)
		free(param->file);
	if (param->name)
		free(param->name);
	free(param);
}

static void
free_data(RPCDATA *data)
{
	int i;
	if (data->spname)
		free(data->spname);
	if (data->paramslen) {
		for (i=0; i<data->paramslen; i++) {
			free_data_param(data->params[i]);
		}
		free(data->params);
	}
	free(data);
}

static void
free_data_paramsvalue(RPCDATA *data)
{
	int i;
	for (i=0; i<data->paramslen; i++) {
		RPCPARAM *param;
		param = data->params[i];
		if (param->value) {
			free(param->value);
			param->value = NULL;
		}
	}
}

static int
read_file(char *name, BYTE **pbuf, size_t *plen, int is_text)
{
	FILE *fp;
	char *buf = NULL;
	size_t len = 0;
	size_t ok;
	char tmp[1048576L];
	size_t maxlen = is_text ? MAX_TEXTSIZE : MAX_BYTESIZE;

	if (!strcmp("-", name)) {
		fp = stdin;
	} else {
		fp = fopen(name, "rb");
		if (!fp) {
			fprintf(stderr, "Can't open input param file: %s\n", name);
			return FALSE;
		}
	}

	while((ok = fread(tmp, 1, 1048576L, fp))) {
		if (len + ok > maxlen)
			ok = maxlen - len;
		buf = xrealloc(buf, len + ok);
		memcpy(buf + len, tmp, ok);
		len += (int)ok;
		if (len >= maxlen)
			break;
	}

	if (fp != stdin)
		fclose(fp);

	/* debug: pad fixed length data */
	if (!is_text && len < maxlen) {
		buf = xrealloc(buf, maxlen);
		memset(buf + len, 0, maxlen - len);
	}

	*pbuf = (BYTE *)buf;
	*plen = len;

	return TRUE;
}

static int
write_file(char *name, BYTE *buf, size_t len)
{
	FILE *fp;
	size_t ok;

	if (!strcmp("-", name)) {
		fp = stdout;
	} else {
		fp = fopen(name, "wb");
		if (!fp) {
			fprintf(stderr, "Can't open output param file: %s\n", name);
			return FALSE;
		}
	}

	ok = len ? fwrite(buf, len, 1, fp) : 1;
	if (fp != stdout)
		fclose(fp);

	if (!ok)
		fprintf(stderr, "Error writing file %s\n", name);

	return TRUE;
}

static char *
to_lowercase(char *s)
{
	int i = 0;
	char *str = strdup(s);

	while (str[i]) {
		if (str[i] >= 'A' && str[i] <= 'Z')
			str[i] += 32;
		i++;
	}
	return (str);
}
