



#define ucs2_t	u_int16_t

/* generic iconv conversion structure */
typedef struct {
        size_t (*direct)(void *cd, char **inbuf, size_t *inbytesleft,
                         char **outbuf, size_t *outbytesleft);
        size_t (*pull)(void *cd, char **inbuf, size_t *inbytesleft,
                       char **outbuf, size_t *outbytesleft);
        size_t (*push)(void *cd, char **inbuf, size_t *inbytesleft,
                       char **outbuf, size_t *outbytesleft);
        void *cd_direct, *cd_pull, *cd_push;
        char *from_name, *to_name;
} *atalk_iconv_t;


/* this defines the charset types used in samba */
typedef enum {CH_UCS2=0, CH_UNIX=1, CH_DISPLAY=2, CH_DOS=3, CH_UTF8=4} charset_t;

#define NUM_CHARSETS 5

/*
 *   for each charset we have a function that pulls from that charset to
 *     a ucs2 buffer, and a function that pushes to a ucs2 buffer
 *     */

struct charset_functions {
        const char *name;
        size_t (*pull)(void *, char **inbuf, size_t *inbytesleft,
                                   char **outbuf, size_t *outbytesleft);
        size_t (*push)(void *, char **inbuf, size_t *inbytesleft,
                                   char **outbuf, size_t *outbytesleft);
        struct charset_functions *prev, *next;
};


extern size_t atalk_iconv(atalk_iconv_t cd, const char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);
atalk_iconv_t atalk_iconv_open(const char *tocode, const char *fromcode);
int atalk_iconv_close (atalk_iconv_t cd);

ucs2_t toupper_w(ucs2_t val);
ucs2_t tolower_w(ucs2_t val);


