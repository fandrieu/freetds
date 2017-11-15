#define RPCRPM_DEFAULTTYPE SYBVARCHAR

enum datatype {
	DATATYPE_STR,
	DATATYPE_LONG,
	DATATYPE_DOUBLE
};

typedef struct keyval {
	const char* key;
	int value;
} RPCKEYVAL;

typedef struct prmpd
{
	char *name;
	int type;
	int output;
	char *file;
	BYTE *value;
	int strlen;
}
RPCPRMPARAMDATA;

typedef struct pd
{
	char *spname;
	RPCPRMPARAMDATA **params;
	int paramslen;
	char *paramname;
	int paramfile;
	int paramtype;
	int paramoutput;
	int paramnull;
	char *interfacesfile;
	int textsize;
	int verbose;
	char *user;
	char *pass;
	char *server;
	char *dbname;
	char *options;
	char *charset;
	char *version;
	int packetsize;
	int Uflag;
	int Iflag;
	int Sflag;
	int Pflag;
	int Tflag;
	int Aflag;
}
RPCPARAMDATA;
