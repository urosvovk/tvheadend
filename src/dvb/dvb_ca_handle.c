/*
 * CA handling
 *
 *  Created on: Dec 12, 2011
 *      Author: urosv
 */
#include <linux/dvb/ca.h>
#include <sys/poll.h>

#include "dvb_ca_handle.h"
#include "src/service.h"
#include "util.h"
#include "en50221.h"
#include "dvb.h"
#include "src/libbitstream/mpeg/psi.h"

#ifdef HAVE_ICONV
#include <iconv.h>
#endif


//////////////////////////////////////////////////////////////////////////////////
static mtime_t i_ca_next_event = 0;
int i_adapter = 0;
mtime_t i_wallclock = 0;

print_type_t i_print_type = -1;
int i_syslog = 0;
int i_verbose = 4;

const char *psz_native_charset = "UTF-8";
const char *psz_dvb_charset = "ISO_8859-1";
const char *psz_provider_name = NULL;
#ifdef HAVE_ICONV
static iconv_t iconv_handle = (iconv_t)-1;
#endif


/**
 * PMTNeedsDescrambling: taken from DVBLAST 2.0. demux.c source code TODO Is this reference enough?
 */
static bool PMTNeedsDescrambling( uint8_t *p_pmt )
{
    uint8_t i;
    uint16_t j;
    uint8_t *p_es;
    const uint8_t *p_desc;

    j = 0;
    while ( (p_desc = descs_get_desc( pmt_get_descs( p_pmt ), j )) != NULL )
    {
        uint8_t i_tag = desc_get_tag( p_desc );
        j++;

        if ( i_tag == 0x9 ) return true;
    }

    i = 0;
    while ( (p_es = pmt_get_es( p_pmt, i )) != NULL )
    {
        i++;
        j = 0;
        while ( (p_desc = descs_get_desc( pmtn_get_descs( p_es ), j )) != NULL )
        {
            uint8_t i_tag = desc_get_tag( p_desc );
            j++;

            if ( i_tag == 0x9 ) return true;
        }
    }

    return false;
}

/*****************************************************************************
 * demux_Iconv
 *****************************************************************************
 * This code is from biTStream's examples and is under the WTFPL (see
 * LICENSE.WTFPL).
 *****************************************************************************/
static char *iconv_append_null(const char *p_string, size_t i_length)
{
    char *psz_string = malloc(i_length + 1);
    memcpy(psz_string, p_string, i_length);
    psz_string[i_length] = '\0';
    return psz_string;
}
char *demux_Iconv(void *_unused, const char *psz_encoding,
                  char *p_string, size_t i_length)
{
#ifdef HAVE_ICONV
    static const char *psz_current_encoding = "";

    char *psz_string, *p;
    size_t i_out_length;

    if (!strcmp(psz_encoding, psz_native_charset))
        return iconv_append_null(p_string, i_length);

    if (iconv_handle != (iconv_t)-1 &&
        strcmp(psz_encoding, psz_current_encoding)) {
        iconv_close(iconv_handle);
        iconv_handle = (iconv_t)-1;
    }

    if (iconv_handle == (iconv_t)-1)
        iconv_handle = iconv_open(psz_native_charset, psz_encoding);
    if (iconv_handle == (iconv_t)-1) {
        tvhlog(LOG_ERR,"dvb","couldn't convert from %s to %s (%m)", psz_encoding,
                psz_native_charset);
        return iconv_append_null(p_string, i_length);
    }

    /* converted strings can be up to six times larger */
    i_out_length = i_length * 6;
    p = psz_string = malloc(i_out_length);
    if (iconv(iconv_handle, &p_string, &i_length, &p, &i_out_length) == -1) {
        tvhlog(LOG_WARNING,"dvb", "couldn't convert from %s to %s (%m)", psz_encoding,
                psz_native_charset);
        free(psz_string);
        return iconv_append_null(p_string, i_length);
    }
    if (i_length)
      tvhlog(LOG_WARNING,"dvb","partial conversion from %s to %s", psz_encoding,
                psz_native_charset);

    *p = '\0';
    return psz_string;
#else
    return iconv_append_null(p_string, i_length);
#endif
}

/*
 * demux_ResendCAPMTs: taken from DVBLAST 2.0. demux.c source code TODO Is this reference enough?
 */
void demux_ResendCAPMTs( void )
{
  /*[urosv] TODO: PMT error and PMT change management handling is not done yet. The code belov was handling this in DVBLAST but the structures used are empty in tvheadend, since tvh has its own*/
  ;
    /*int i;
    for ( i = 0; i < i_nb_sids; i++ )
        if ( pp_sids[i]->p_current_pmt != NULL
              && SIDIsSelected( pp_sids[i]->i_sid )
              && PMTNeedsDescrambling( pp_sids[i]->p_current_pmt ) )
            en50221_AddPMT( pp_sids[i]->p_current_pmt );*/
}

/**
 *
 */
void dvb_adapter_ca_init(void *aux)
{
  printf("uros dvb_adapter_ca_init start....\n");
  th_dvb_adapter_t *tda = aux;
	pthread_t ptid;
	pthread_create(&ptid, NULL, dvb_ca_control, tda);
}



/**
 * Thread processing all CAM module communication
 */
void * dvb_ca_control(void *aux)
{
  th_dvb_adapter_t *tda = aux;
  mtime_t i_poll_timeout = MAX_POLL_TIMEOUT;

  pthread_mutex_init(&pmtcmd_pending_que_mutex, NULL);
  pthread_mutex_lock(&pmtcmd_pending_que_mutex);
  SIMPLEQ_INIT(&pmtcmd_pending_que);
  pthread_mutex_unlock(&pmtcmd_pending_que_mutex);


  tvhlog(LOG_DEBUG, "dvb", "dvb_ca_control: calling en50221_Init\n");
  i_adapter = tda->tda_adapter_num; /* The only input for en50221_Init function*/
  en50221_Init();

  i_wallclock = mdate();
  i_ca_next_event = mdate() + CA_POLL_PERIOD;

  while(1) {

		struct pollfd ufds[1];
		int i_ret, i_nb_fd = 0;

		memset( ufds, 0, sizeof(ufds) );
		if ( i_ca_handle && i_ca_type == CA_CI_LINK )
		{
			 ufds[i_nb_fd].fd = i_ca_handle;
			 ufds[i_nb_fd].events = POLLIN;
			 i_nb_fd++;
		}

		i_ret = poll( ufds, i_nb_fd, (i_poll_timeout + 999) / 1000 );

		i_wallclock = mdate();

		if ( i_ret < 0 )
		{
			 if( errno != EINTR )
					 tvhlog(LOG_ERR, "dvb", "dvb_ca_control: poll error: %s", strerror(errno) );
			 return NULL;
		}


		if ( i_ca_handle && i_ca_type == CA_CI_LINK )
		{
       if(pthread_mutex_lock(&tda->adapter_access_ca) == 0)
       {
         //printf("ca>"); // TODO uros remove
         /*Check if there are any pending PMT commands to sent to CA device and send them.*/
         process_pending_PMTs();

         if ( ufds[i_nb_fd - 1].revents )
         {
             en50221_Read();
             i_ca_next_event = i_wallclock + CA_POLL_PERIOD;
         }
         else if ( i_wallclock > i_ca_next_event )
         {
             en50221_Poll();
             i_ca_next_event = i_wallclock + CA_POLL_PERIOD;
         }
         //printf("<ca\n"); // TODO uros remove

         /*Check if there are any pending PMT commands to sent to CA device and send them.*/
         process_pending_PMTs();
         pthread_mutex_unlock(&tda->adapter_access_ca);
       }
		}

		// TODO [urosv] en50221_Reset() should be called on certain tvh events like FE reset/retune, DVR out of stream...
  } // end while(1)

  // TODO [urosv] Proper thread exit should be implemented
  return NULL;
}

/**
 * ResetCAM
 */
void ResetCAM(th_dvb_adapter_t *tda)
{
  pthread_mutex_lock(&tda->adapter_access_ca);
  en50221_Reset();
  pthread_mutex_unlock(&tda->adapter_access_ca);

  clear_pending_PMTs();
}

/**
 * Send PMT commands to dvb CA device from another thread: this call just puts it onto a que.
 */
bool add_delete_update_PMT_delayed(uint8_t *p_pmt, e_delayed_PMT_action_t pmtaction)
{
	pmtcmd_item_t *item = malloc(sizeof(pmtcmd_item_t));
	item->action = pmtaction;
	memcpy(item->p_pmt_buffer, p_pmt, sizeof(item->p_pmt_buffer));

	pthread_mutex_lock(&pmtcmd_pending_que_mutex);
	SIMPLEQ_INSERT_TAIL(&pmtcmd_pending_que, item, link);
	pthread_mutex_unlock(&pmtcmd_pending_que_mutex);

	return true;
}

/**
 * Send queued PMT commands to dvb CA device.
 */
int process_pending_PMTs(void)
{
  int countPMTs = 0;

  pthread_mutex_lock(&pmtcmd_pending_que_mutex);
  while (!SIMPLEQ_EMPTY(&pmtcmd_pending_que))
  {
    pmtcmd_item_t *pmtcmd = SIMPLEQ_FIRST(&pmtcmd_pending_que);
    switch ((int)pmtcmd->action)
    {
      case (int)eacPMTundefined:
        break;
      case (int)eacPMTadd:
        printf("uros en50221_AddPMT start\n");
        tvhlog(LOG_DEBUG, "dvb", "Calling en50221_AddPMT ...\n");
        en50221_AddPMT( pmtcmd->p_pmt_buffer );
        tvhlog(LOG_DEBUG, "dvb", "en50221_AddPMT ended.\n");
        printf("uros en50221_AddPMT end\n");
        break;
      case (int)eacPMTdelete:
        printf("uros en50221_DeletePMT start\n");
        tvhlog(LOG_DEBUG, "dvb", "Calling en50221_DeletePMT ...\n");
        en50221_DeletePMT( pmtcmd->p_pmt_buffer );
        tvhlog(LOG_DEBUG, "dvb", "en50221_DeletePMT ended.\n");
        printf("uros en50221_DeletePMT end\n");
        break;
    }

    SIMPLEQ_REMOVE(&pmtcmd_pending_que, pmtcmd, pmtcmd_item, link);
    free(pmtcmd);

    countPMTs++;
  }

  pthread_mutex_unlock(&pmtcmd_pending_que_mutex);
  return countPMTs;
}

/**
 * Clear queued PMT commands for dvb CA device.
 */
void clear_pending_PMTs(void)
{

  pthread_mutex_lock(&pmtcmd_pending_que_mutex);
  while (!SIMPLEQ_EMPTY(&pmtcmd_pending_que))
  {
    pmtcmd_item_t *pmtcmd = SIMPLEQ_FIRST(&pmtcmd_pending_que);
    SIMPLEQ_REMOVE(&pmtcmd_pending_que, pmtcmd, pmtcmd_item, link);
    free(pmtcmd);
  }
  pthread_mutex_unlock(&pmtcmd_pending_que_mutex);
}

/*
 *
 * Clean up the dummy descrambler
 */
static void
ca_descrambler_stop(struct th_descrambler *td)
{
  LIST_REMOVE(td, td_service_link);
  free(td);
}
/**
 *
 */
static void
ca_descrambler_table_input(struct th_descrambler *td, struct service *s,
		   struct elementary_stream *es,
		   const uint8_t *section, int section_len)
{
	;
}

/**
 *
 */
static int
ca_descrambler_dummydescramble(struct th_descrambler *td, struct service *s, struct elementary_stream *es,
     const uint8_t *tsb)
{
	/* [urosv] This dummy is only allowed for small time, until CAM module starts descrambling: typically a second or two, max 5 by the standard.
	 * A timeout is implemented, which is linked to input th_descrambler_t object
	 * Without this timeout the end user would be watching silence and dark indefinetly if the CAM module can not descramble the selected channel.
	 * */
	mtime_t currenttime = mdate();
	const mtime_t alloweddelay = 5000000; // usec
	if (td->time_of_first_descramble_call == 0) {
		td->time_of_first_descramble_call = currenttime;
		tvhlog(LOG_INFO,"dvb","Dummy descrambler started at %dmsec\n", (int)(currenttime/1000));
	} else if (td->time_of_first_descramble_call + alloweddelay < currenttime) {
		/* Dummy descrambler should kill itself */
		ca_descrambler_stop(td);
		tvhlog(LOG_INFO,"dvb","Dummy descrambler stopped at %dmsec\n", (int)(currenttime/1000));
	}

	/* Update the descrambling flag according to live stream */
	if( (tsb[3] & 0xc0) == 0) { /*Check for scrambled flag in stream packet and turn descrambling off if it is no longer scrambled: it is probably descrambled by CAM module*/
		s->s_scrambled_seen = 0;
	}

	return 0;
}
/*
 * Configure DVB CA device to start descrambling the given transport stream
 * */
bool start_transport_descrambling(struct service *s)
{
  bool ret = true;

  uint8_t p_pmt[MAX_PMTCMD_BUF_SIZE];
  memset(p_pmt, 0xff, MAX_PMTCMD_BUF_SIZE);
  psi_build_pmt_fordescrambling(s, p_pmt, MAX_PMTCMD_BUF_SIZE);

  if (PMTNeedsDescrambling(p_pmt) )
  {
    bool compatible_scrambling = false;
    th_dvb_adapter_t *tda = s->s_dvb_mux_instance->tdmi_adapter;
    if(pthread_mutex_lock(&tda->adapter_access_ca) == 0) {
      compatible_scrambling = CheckForCompatibleScramblingSystem(p_pmt);
      pthread_mutex_unlock(&tda->adapter_access_ca);
    }

    if (compatible_scrambling) {
      /*Set up a dummy descrambler, that just throws the packets away until the actual CAM module starts descrambling.
       * */
      th_descrambler_t *td = malloc(sizeof(th_descrambler_t));
      td->td_stop       = ca_descrambler_stop;
      td->td_table      = ca_descrambler_table_input;
      td->td_descramble = ca_descrambler_dummydescramble;
      td->time_of_first_descramble_call = 0; /*0 - uninitialized*/
      LIST_INSERT_HEAD(&s->s_descramblers, td, td_service_link);

      ret = add_delete_update_PMT_delayed(p_pmt, eacPMTadd);
    } else { printf("uros no compatible descrambling system caids!\n"); }

  } else {
    ;//ret = add_delete_update_PMT_delayed(p_pmt, eacPMTstop);
  }
  return ret;
}
/*
 * Configure DVB CA device to stop descrambling given transport stream
 * */
bool stop_transport_descrambling(struct service *s)
{
    bool ret = true;

    uint8_t p_pmt[MAX_PMTCMD_BUF_SIZE];
    memset(p_pmt, 0xff, MAX_PMTCMD_BUF_SIZE);
    psi_build_pmt_fordescrambling(s, p_pmt, MAX_PMTCMD_BUF_SIZE);

    if (PMTNeedsDescrambling(p_pmt))
    {
      bool compatible_scrambling = false;
      th_dvb_adapter_t *tda = s->s_dvb_mux_instance->tdmi_adapter;
      if(pthread_mutex_lock(&tda->adapter_access_ca) == 0) {
        compatible_scrambling = CheckForCompatibleScramblingSystem(p_pmt);
        pthread_mutex_unlock(&tda->adapter_access_ca);
      }

      if (compatible_scrambling) {
        ret = add_delete_update_PMT_delayed(p_pmt, eacPMTdelete);
      }
    } else {
      ;//ret = add_delete_update_PMT_delayed(p_pmt, eacPMTstop);
    }
    return ret;
}


