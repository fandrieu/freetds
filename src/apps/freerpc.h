#define RPCPARAM_DEFAULTTYPE SYBVARCHAR
#define MAX_TEXTSIZE 2147483647
#define MAX_BYTESIZE 16

/* internal param type used for allocating and printing debug */
enum rpc_datatype {
	DATATYPE_STR,    /* textsize, dbg %s */
	DATATYPE_LONG,   /* bytesize, dbg %lld */
	DATATYPE_DOUBLE, /* bytesize, dbg %f */
	DATATYPE_BYTES,  /* bytesize, dbg %x */
};

typedef struct rpc_keyval {
	const char* key;
	int value;
} RPCKEYVAL;

typedef struct rpc_data_param
{
	char *name;
	int type;
	unsigned int output:1;
	char *file;
	BYTE *value;
	DBINT valuelen;
}
RPCPARAM;

typedef struct rpc_data
{
	char *spname;
	RPCPARAM **params;
	FILE *fpout;
	int paramslen;
	int status;
	int verbose;
}
RPCDATA;

typedef struct rpc_paramopts
{
	char *name;
	int type;
	unsigned int file:1;
	unsigned int output:1;
	unsigned int null:1;
	int stdincheck;
}
RPCPARAMOPTS;

typedef struct rpc_login
{
	char *interfacesfile;
	char *user;
	char *pass;
	char *server;
	char *dbname;
	char *options;
	char *charset;
	char *version;
	int packetsize;
	int textsize;
}
RPCLOGIN;
