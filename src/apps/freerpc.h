typedef struct prmpd
{
	char *name;
	int type;
	int output;
	BYTE *value;
}
RPCPRMPARAMDATA;

typedef struct pd
{
	char *spname;
	RPCPRMPARAMDATA **params;
	int paramslen;
	char *paramname;
	int paramnull;
	char *interfacesfile;
	int textsize;
	char *user;
	char *pass;
	char *server;
	char *dbname;
	char *options;
	char *charset;
	int packetsize;
	int Uflag;
	int Iflag;
	int Sflag;
	int Pflag;
	int Tflag;
	int Aflag;
}
RPCPARAMDATA;
