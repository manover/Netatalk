/*
 * $Id: ad_attr.c,v 1.4.8.3 2004-02-06 13:39:52 bfernhomberg Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <atalk/adouble.h>

#define FILEIOFF_ATTR 14
#define AFPFILEIOFF_ATTR 2

int ad_getattr(const struct adouble *ad, u_int16_t *attr)
{
  if (ad->ad_version == AD_VERSION1)
    memcpy(attr, ad_entry(ad, ADEID_FILEI) + FILEIOFF_ATTR,
	   sizeof(u_int16_t));
#if AD_VERSION == AD_VERSION2
  else if (ad->ad_version == AD_VERSION2)
    memcpy(attr, ad_entry(ad, ADEID_AFPFILEI) + AFPFILEIOFF_ATTR,
	   sizeof(u_int16_t));
#endif
  else 
    return -1;

  return 0;
}

int ad_setattr(const struct adouble *ad, const u_int16_t attr)
{
  if (ad->ad_version == AD_VERSION1)
    memcpy(ad_entry(ad, ADEID_FILEI) + FILEIOFF_ATTR, &attr,
	   sizeof(attr));
#if AD_VERSION == AD_VERSION2
  else if (ad->ad_version == AD_VERSION2)
    memcpy(ad_entry(ad, ADEID_AFPFILEI) + AFPFILEIOFF_ATTR, &attr,
	   sizeof(attr));
#endif
  else 
    return -1;

  return 0;
}

/* -------------- 
 * save file/folder ID in AppleDoubleV2 netatalk private parameters
*/
#if AD_VERSION == AD_VERSION2
int ad_setid (struct adouble *adp, const dev_t dev, const ino_t ino , const u_int32_t id, const cnid_t did, const void *stamp)
{
    if (adp->ad_flags == AD_VERSION2 && sizeof(dev_t) == ADEDLEN_PRIVDEV && sizeof(ino_t) == ADEDLEN_PRIVINO) 
    {
        ad_setentrylen( adp, ADEID_PRIVDEV, sizeof(dev_t));
        memcpy(ad_entry( adp, ADEID_PRIVDEV ), &dev, sizeof(dev_t));

        ad_setentrylen( adp, ADEID_PRIVINO, sizeof(ino_t));
        memcpy(ad_entry( adp, ADEID_PRIVINO ), &ino, sizeof(ino_t));

	ad_setentrylen( adp, ADEID_PRIVID, sizeof(id));
        memcpy(ad_entry( adp, ADEID_PRIVID ), &id, sizeof(id));

        ad_setentrylen( adp, ADEID_DID, sizeof(did));
        memcpy(ad_entry( adp, ADEID_DID ), &did, sizeof(did));

        ad_setentrylen( adp, ADEID_PRIVSYN, ADEDLEN_PRIVSYN);
        memcpy(ad_entry( adp, ADEID_PRIVSYN ), stamp, ADEDLEN_PRIVSYN);
    }
    return 0;
}

#endif
