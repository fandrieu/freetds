#define RPCPARAM_DEFAULTTYPE SYBVARCHAR

enum rpc_datatype {
	DATATYPE_STR,
	DATATYPE_LONG,
	DATATYPE_DOUBLE
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
	int strlen;
}
RPCPARAM;

typedef struct rpc_data
{
	char *spname;
	RPCPARAM **params;
	int paramslen;
	int verbose;
	int textsize;
}
RPCDATA;

typedef struct rpc_paramopts
{
	char *name;
	int type;
	unsigned int file:1;
	unsigned int output:1;
	unsigned int null:1;
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
}
RPCLOGIN;
