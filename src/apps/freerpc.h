#define RPCPARAM_DEFAULTTYPE SYBVARCHAR
#define MAX_TEXTSIZE 2147483647
#define MAX_BYTESIZE 8

enum rpc_datatype {
	DATATYPE_STR,
	DATATYPE_LONG,
	DATATYPE_DOUBLE,
	DATATYPE_BYTES
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
	DBINT strlen;
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
