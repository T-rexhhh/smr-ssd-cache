#include <stdio.h>
#include <stdlib.h>
#include "ssd-cache.h"
#include "smr-simulator/smr-simulator.h"
#include "ssd_buf_table_for_coldmax_history.h"
#include "maxcold.h"

static volatile void *addToLRUHeadNow(SSDBufferDescForMaxColdNow * ssd_buf_hdr_for_maxcold);
static volatile void *deleteFromLRUNow(SSDBufferDescForMaxColdNow * ssd_buf_hdr_for_maxcold);
static volatile void *moveToLRUHeadNow(SSDBufferDescForMaxColdNow * ssd_buf_hdr_for_maxcold);
static volatile void *addToLRUHeadHistory(SSDBufferDescForMaxColdHistory * ssd_buf_hdr_for_maxcold);
static volatile void *deleteFromLRUHistory(SSDBufferDescForMaxColdHistory * ssd_buf_hdr_for_maxcold);
static volatile void *moveToLRUHeadHistory(SSDBufferDescForMaxColdHistory * ssd_buf_hdr_for_maxcold);
static volatile void *addToLRUHead(SSDBufferDescForLRURead * ssd_buf_hdr_for_lru_read);
static volatile void *deleteFromLRU(SSDBufferDescForLRURead * ssd_buf_hdr_for_lru_read);
static volatile void *moveToLRUHead(SSDBufferDescForLRURead * ssd_buf_hdr_for_lru_read);
static volatile void *pause_and_caculate_next_period_hotdivsize();
static SSDBufferDesc *getMaxColdBufferRead(SSDBufferTag new_ssd_buf_tag);
static SSDBufferDesc *getMaxColdBufferWrite(SSDBufferTag new_ssd_buf_tag);
static void          *hitInMaxColdBufferRead(SSDBufferDesc * ssd_buf_hdr);
static void          *hitInMaxColdBufferWrite(SSDBufferDesc * ssd_buf_hdr);

static volatile unsigned long
GetSMRZoneNumFromSSD(size_t offset)
{
    return offset / ZONESZ;
}

static volatile void *
resetSSDBufferForMaxColdHistory()
{
	SSDBufferDescForMaxColdHistory *ssd_buf_hdr_for_maxcold_history;
	BandDescForMaxColdHistory *band_hdr_for_maxcold_history;
	long		i, next;
    long        total_history_ssds = ssd_buffer_strategy_control_for_maxcold_history->n_usedssds;

	band_hdr_for_maxcold_history = band_descriptors_for_maxcold_history;
	for (i = 0; i < NSMRBands; band_hdr_for_maxcold_history++, i++) {
		band_hdr_for_maxcold_history->band_num = i;
		band_hdr_for_maxcold_history->current_hits = 0;
		band_hdr_for_maxcold_history->current_pages = 0;
		band_hdr_for_maxcold_history->current_cold_pages = 0;
        band_hdr_for_maxcold_history->to_sort = 0;
	}

    next = ssd_buffer_strategy_control_for_maxcold_history->first_lru; 
    for (i = 0; i < total_history_ssds; i++) {
        ssd_buf_hdr_for_maxcold_history = &ssd_buffer_descriptors_for_maxcold_history[next];
        unsigned long   ssd_buf_hash = ssdbuftableHashcode(&ssd_buf_hdr_for_maxcold_history->ssd_buf_tag);
        long            ssd_buf_id = ssdbuftableLookup(&ssd_buf_hdr_for_maxcold_history->ssd_buf_tag, ssd_buf_hash);
        next = ssd_buf_hdr_for_maxcold_history->next_lru;
        if (ssd_buf_id < 0) {
	        SSDBufferTag	ssd_buf_tag_history = ssd_buf_hdr_for_maxcold_history->ssd_buf_tag;
	        unsigned long	ssd_buf_hash_history = ssdbuftableHashcodeHistory(&ssd_buf_tag_history);
	        long		ssd_buf_id_history = ssdbuftableLookupHistory(&ssd_buf_tag_history, ssd_buf_hash_history);
		    ssdbuftableDeleteHistory(&ssd_buf_tag_history, ssd_buf_hash_history);
            ssd_buf_hdr_for_maxcold_history->ssd_buf_flag = 0;
            deleteFromLRUHistory(ssd_buf_hdr_for_maxcold_history);
            ssd_buf_hdr_for_maxcold_history->next_freessd = ssd_buffer_strategy_control_for_maxcold_history->first_freessd;
            ssd_buffer_strategy_control_for_maxcold_history->first_freessd = ssd_buf_hdr_for_maxcold_history->ssd_buf_id;
        }
    }

	return NULL;
}

static volatile void *
resetSSDBufferForMaxColdNow()
{
    SSDBufferDescForMaxColdNow *ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[ssd_buffer_strategy_control_for_maxcold_now->first_lru];
	long		i, next;
    for (i = 0; i < ssd_buffer_strategy_control_for_maxcold_now->n_usedssds; i++) {
        next = ssd_buf_hdr_for_maxcold_now->next_lru;
        deleteFromLRUNow(ssd_buf_hdr_for_maxcold_now);
        ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[next];
    }

	ssd_buffer_strategy_control_for_maxcold_now->first_lru = -1;
	ssd_buffer_strategy_control_for_maxcold_now->last_lru = -1;
	ssd_buffer_strategy_control_for_maxcold_now->n_usedssds = 0;

	ssd_buf_hdr_for_maxcold_now = ssd_buffer_descriptors_for_maxcold_now;
	for (i = 0; i < NSSDBuffers; ssd_buf_hdr_for_maxcold_now++, i++) {
		ssd_buf_hdr_for_maxcold_now->next_lru = -1;
		ssd_buf_hdr_for_maxcold_now->last_lru = -1;
	}
    
    SSDBufferDescForMaxColdHistory *ssd_buf_hdr_for_maxcold_history = &ssd_buffer_descriptors_for_maxcold_history[ssd_buffer_strategy_control_for_maxcold_history->first_lru];
    next = ssd_buffer_strategy_control_for_maxcold_now->first_lru; 
    for (i = 0; i < ssd_buffer_strategy_control_for_maxcold_history->n_usedssds; i++) {
        unsigned long   band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr_for_maxcold_history->ssd_buf_tag.offset);
        unsigned long   ssd_buf_hash = ssdbuftableHashcode(&ssd_buf_hdr_for_maxcold_history->ssd_buf_tag);
        long            ssd_buf_id = ssdbuftableLookup(&ssd_buf_hdr_for_maxcold_history->ssd_buf_tag, ssd_buf_hash);
        SSDBufferDesc   *ssd_buf_hdr = &ssd_buffer_descriptors[ssd_buf_id];
	    if ((ssd_buf_hdr->ssd_buf_flag & SSD_BUF_ISCHOSEN) > 0) {
            ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[ssd_buf_id];
            addToLRUHeadNow(ssd_buf_hdr_for_maxcold_now);
        }
        next = ssd_buf_hdr_for_maxcold_history->next_lru;
        ssd_buf_hdr_for_maxcold_history = &ssd_buffer_descriptors_for_maxcold_history[next];
    }

	return NULL;
}

/*
 * init buffer hash table, strategy_control, buffer, work_mem
 */
void
initSSDBufferForMaxColdSplitRW()
{
	initSSDBufTableHistory(NSSDBufTables * 5);

	ssd_buffer_strategy_control_for_maxcold_history = (SSDBufferStrategyControlForMaxColdHistory *) malloc(sizeof(SSDBufferStrategyControlForMaxColdHistory));
	ssd_buffer_strategy_control_for_maxcold_now = (SSDBufferStrategyControlForMaxColdNow *) malloc(sizeof(SSDBufferStrategyControlForMaxColdNow));
	ssd_buffer_strategy_control_for_lru_read = (SSDBufferStrategyControlForLRURead *) malloc(sizeof(SSDBufferStrategyControlForLRURead));
	ssd_buffer_descriptors_for_maxcold_history = (SSDBufferDescForMaxColdHistory *) malloc(sizeof(SSDBufferDescForMaxColdHistory) * (NSSDBuffersRead + NSSDBuffersWrite) * ((PERIODTIMES - 1) / NSSDBuffers + 2));
	ssd_buffer_descriptors_for_maxcold_now = (SSDBufferDescForMaxColdNow *) malloc(sizeof(SSDBufferDescForMaxColdNow) * NSSDBuffers);
	ssd_buffer_descriptors_for_lru_read = (SSDBufferDescForLRURead *) malloc(sizeof(SSDBufferDescForLRURead) * (NSSDBuffersRead + NSSDBuffersWrite));
	band_descriptors_for_maxcold_history = (BandDescForMaxColdHistory *) malloc(sizeof(BandDescForMaxColdHistory) * NSMRBands);
	band_descriptors_for_maxcold_now = (BandDescForMaxColdNow *) malloc(sizeof(BandDescForMaxColdNow) * NSMRBands);

	/* At first, all data pages in SMR can be chosen for evict, and the strategy is actually LRU. */
    BandDescForMaxColdNow * band_hdr_for_maxcold_now;
	long		i;
	band_hdr_for_maxcold_now = band_descriptors_for_maxcold_now;
	for (i = 0; i < NSMRBands; band_hdr_for_maxcold_now++, i++) {
		band_hdr_for_maxcold_now->ischosen = 1;
	}

    /* init ssd_buffer_strategy_control_for_maxcold_now & ssd_buffer_descriptors_for_maxcold_now */
	ssd_buffer_strategy_control_for_maxcold_now->first_lru = -1;
	ssd_buffer_strategy_control_for_maxcold_now->last_lru = -1;
	ssd_buffer_strategy_control_for_maxcold_now->n_usedssds = 0;

	SSDBufferDescForMaxColdNow *ssd_buf_hdr_for_maxcold_now;

	ssd_buf_hdr_for_maxcold_now = ssd_buffer_descriptors_for_maxcold_now;
	for (i = 0; i < NSSDBuffers; ssd_buf_hdr_for_maxcold_now++, i++) {
		ssd_buf_hdr_for_maxcold_now->ssd_buf_id = i;
		ssd_buf_hdr_for_maxcold_now->next_lru = -1;
		ssd_buf_hdr_for_maxcold_now->last_lru = -1;
	}
	
    /* init ssd_buffer_strategy_control_for_maxcold_history & ssd_buffer_descriptors_for_maxcold_history */
    ssd_buffer_strategy_control_for_maxcold_history->first_lru = -1;
	ssd_buffer_strategy_control_for_maxcold_history->last_lru = -1;
    ssd_buffer_strategy_control_for_maxcold_history->first_freessd = 0;
	ssd_buffer_strategy_control_for_maxcold_history->last_freessd = (NSSDBuffersRead + NSSDBuffersWrite) * ((PERIODTIMES - 1) / NSSDBuffers + 2)- 1;
	ssd_buffer_strategy_control_for_maxcold_history->n_usedssds = 0;

	SSDBufferDescForMaxColdHistory *ssd_buf_hdr_for_maxcold_history;
	BandDescForMaxColdHistory *band_hdr_for_maxcold_history;

	ssd_buf_hdr_for_maxcold_history = ssd_buffer_descriptors_for_maxcold_history;
	for (i = 0; i < (NSSDBuffersRead + NSSDBuffersWrite) * ((PERIODTIMES - 1) / NSSDBuffers + 2); ssd_buf_hdr_for_maxcold_history++, i++) {
		ssd_buf_hdr_for_maxcold_history->ssd_buf_tag.offset = 0;
        ssd_buf_hdr_for_maxcold_history->ssd_buf_flag = 0;
		ssd_buf_hdr_for_maxcold_history->ssd_buf_id = i;
		ssd_buf_hdr_for_maxcold_history->next_lru = -1;
		ssd_buf_hdr_for_maxcold_history->last_lru = -1;
		ssd_buf_hdr_for_maxcold_history->next_freessd = i+1;
		ssd_buf_hdr_for_maxcold_history->hit_times = 0;
	}
	ssd_buf_hdr_for_maxcold_history->next_freessd = -1;

	band_hdr_for_maxcold_history = band_descriptors_for_maxcold_history;
	for (i = 0; i < NSMRBands; band_hdr_for_maxcold_history++, i++) {
		band_hdr_for_maxcold_history->band_num = i;
		band_hdr_for_maxcold_history->current_hits = 0;
		band_hdr_for_maxcold_history->current_pages = 0;
		band_hdr_for_maxcold_history->current_cold_pages = 0;
		band_hdr_for_maxcold_history->to_sort = 0;
	}

    /* init ssd_buffer_strategy_control_for_lru_read & ssd_buffer_descriptors_for_lru_read */
    ssd_buffer_strategy_control_for_lru_read->first_lru = -1;
	ssd_buffer_strategy_control_for_lru_read->last_lru = -1;
    ssd_buffer_strategy_control_for_lru_read->first_freessd = NSSDBuffersWrite;
	ssd_buffer_strategy_control_for_lru_read->last_freessd = NSSDBuffersRead + NSSDBuffersWrite;
	ssd_buffer_strategy_control_for_lru_read->n_usedssds = 0;

	SSDBufferDescForLRURead *ssd_buf_hdr_for_lru_read;

	ssd_buf_hdr_for_lru_read = &ssd_buffer_descriptors_for_lru_read[NSSDBuffersWrite];
	for (i = NSSDBuffersWrite; i < NSSDBuffersRead + NSSDBuffersWrite; ssd_buf_hdr_for_lru_read++, i++) {
		ssd_buf_hdr_for_lru_read->ssd_buf_id = i;
		ssd_buf_hdr_for_lru_read->next_lru = -1;
		ssd_buf_hdr_for_lru_read->last_lru = -1;
	}

	flush_fifo_times = 0;
}

static volatile void *
addToLRUHeadHistory(SSDBufferDescForMaxColdHistory * ssd_buf_hdr_for_maxcold_history)
{
//    printf("in addToLRUHeadHistory(): ssd_buffer_strategy_control_for_maxcold_history->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_maxcold_history->n_usedssds);
	if (ssd_buffer_strategy_control_for_maxcold_history->n_usedssds == 0) {
		ssd_buffer_strategy_control_for_maxcold_history->first_lru = ssd_buf_hdr_for_maxcold_history->ssd_buf_id;
		ssd_buffer_strategy_control_for_maxcold_history->last_lru = ssd_buf_hdr_for_maxcold_history->ssd_buf_id;
	} else {
		ssd_buf_hdr_for_maxcold_history->next_lru = ssd_buffer_descriptors_for_maxcold_history[ssd_buffer_strategy_control_for_maxcold_history->first_lru].ssd_buf_id;
		ssd_buf_hdr_for_maxcold_history->last_lru = -1;
		ssd_buffer_descriptors_for_maxcold_history[ssd_buffer_strategy_control_for_maxcold_history->first_lru].last_lru = ssd_buf_hdr_for_maxcold_history->ssd_buf_id;
		ssd_buffer_strategy_control_for_maxcold_history->first_lru = ssd_buf_hdr_for_maxcold_history->ssd_buf_id;
	}
    ssd_buffer_strategy_control_for_maxcold_history->n_usedssds ++;

	return NULL;
}

static volatile void *
deleteFromLRUHistory(SSDBufferDescForMaxColdHistory * ssd_buf_hdr_for_maxcold_history)
{
//    printf("in deleteFromLRUHistory(): ssd_buffer_strategy_control_for_maxcold_history->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_maxcold_history->n_usedssds);
	if (ssd_buf_hdr_for_maxcold_history->last_lru >= 0) {
		ssd_buffer_descriptors_for_maxcold_history[ssd_buf_hdr_for_maxcold_history->last_lru].next_lru = ssd_buf_hdr_for_maxcold_history->next_lru;
	} else {
		ssd_buffer_strategy_control_for_maxcold_history->first_lru = ssd_buf_hdr_for_maxcold_history->next_lru;
	}
	if (ssd_buf_hdr_for_maxcold_history->next_lru >= 0) {
		ssd_buffer_descriptors_for_maxcold_history[ssd_buf_hdr_for_maxcold_history->next_lru].last_lru = ssd_buf_hdr_for_maxcold_history->last_lru;
	} else {
		ssd_buffer_strategy_control_for_maxcold_history->last_lru = ssd_buf_hdr_for_maxcold_history->last_lru;
	}
    ssd_buffer_strategy_control_for_maxcold_history->n_usedssds --;

	return NULL;
}

static volatile void *
moveToLRUHeadHistory(SSDBufferDescForMaxColdHistory * ssd_buf_hdr_for_maxcold_history)
{
	deleteFromLRUHistory(ssd_buf_hdr_for_maxcold_history);
	addToLRUHeadHistory(ssd_buf_hdr_for_maxcold_history);

	return NULL;
}

static volatile void *
addToLRUHeadNow(SSDBufferDescForMaxColdNow * ssd_buf_hdr_for_maxcold_now)
{
//    printf("in addToLRUHeadNow(): ssd_buffer_strategy_control_for_maxcold_now->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);

	if (ssd_buffer_strategy_control_for_maxcold_now->n_usedssds == 0) {
		ssd_buffer_strategy_control_for_maxcold_now->first_lru = ssd_buf_hdr_for_maxcold_now->ssd_buf_id;
		ssd_buffer_strategy_control_for_maxcold_now->last_lru = ssd_buf_hdr_for_maxcold_now->ssd_buf_id;
	} else {
		ssd_buf_hdr_for_maxcold_now->next_lru = ssd_buffer_descriptors_for_maxcold_now[ssd_buffer_strategy_control_for_maxcold_now->first_lru].ssd_buf_id;
		ssd_buf_hdr_for_maxcold_now->last_lru = -1;
		ssd_buffer_descriptors_for_maxcold_now[ssd_buffer_strategy_control_for_maxcold_now->first_lru].last_lru = ssd_buf_hdr_for_maxcold_now->ssd_buf_id;
		ssd_buffer_strategy_control_for_maxcold_now->first_lru = ssd_buf_hdr_for_maxcold_now->ssd_buf_id;
	}
    ssd_buffer_strategy_control_for_maxcold_now->n_usedssds ++;

    return NULL;
}

static volatile void *
deleteFromLRUNow(SSDBufferDescForMaxColdNow * ssd_buf_hdr_for_maxcold_now)
{
//    printf("in deleteFromLRUNow(): ssd_buffer_strategy_control_for_maxcold_now->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);
    long i;

	if (ssd_buf_hdr_for_maxcold_now->last_lru >= 0) {
		ssd_buffer_descriptors_for_maxcold_now[ssd_buf_hdr_for_maxcold_now->last_lru].next_lru = ssd_buf_hdr_for_maxcold_now->next_lru;
	} else {
		ssd_buffer_strategy_control_for_maxcold_now->first_lru = ssd_buf_hdr_for_maxcold_now->next_lru;
	}
	if (ssd_buf_hdr_for_maxcold_now->next_lru >= 0) {
		ssd_buffer_descriptors_for_maxcold_now[ssd_buf_hdr_for_maxcold_now->next_lru].last_lru = ssd_buf_hdr_for_maxcold_now->last_lru;
	} else {
		ssd_buffer_strategy_control_for_maxcold_now->last_lru = ssd_buf_hdr_for_maxcold_now->last_lru;
	}
    ssd_buffer_strategy_control_for_maxcold_now->n_usedssds --;

	return NULL;
}

static volatile void *
moveToLRUHeadNow(SSDBufferDescForMaxColdNow * ssd_buf_hdr_for_maxcold_now)
{
	deleteFromLRUNow(ssd_buf_hdr_for_maxcold_now);
	addToLRUHeadNow(ssd_buf_hdr_for_maxcold_now);

	return NULL;
}

static volatile void *
qsort_band_history(long start, long end)
{
	long		i = start;
	long		j = end;
	BandDescForMaxColdHistory x = band_descriptors_for_maxcold_history[i];
	while (i < j) {
		while (band_descriptors_for_maxcold_history[j].to_sort <= x.to_sort && i < j)
			j--;
		if (i < j)
			band_descriptors_for_maxcold_history[i] = band_descriptors_for_maxcold_history[j];
		while (band_descriptors_for_maxcold_history[i].to_sort >= x.to_sort && i < j)
			i++;
		if (i < j)
			band_descriptors_for_maxcold_history[j] = band_descriptors_for_maxcold_history[i];
	}
	band_descriptors_for_maxcold_history[i] = x;
	if (i - 1 > start)
		qsort_band_history(start, i - 1);
	if (j + 1 < end)
		qsort_band_history(j + 1, end);
}

static volatile long
find_non_empty()
{
    long        i = 0, j = NSMRBands - 1;
    BandDescForMaxColdHistory tmp;

    while (i < j) {
        while (i < j && band_descriptors_for_maxcold_history[j].to_sort == 0)
            j--;
        while (i < j && band_descriptors_for_maxcold_history[i].to_sort != 0)
            i++;
        if (i < j) {
            tmp = band_descriptors_for_maxcold_history[i];
            band_descriptors_for_maxcold_history[i] = band_descriptors_for_maxcold_history[j];
            band_descriptors_for_maxcold_history[j] = tmp;
        }
    }

    return i;
}

static volatile void *
pause_and_caculate_next_period_hotdivsize()
{
	unsigned long	band_num;
	SSDBufferDescForMaxColdHistory *ssd_buf_hdr_for_maxcold_history;
	SSDBufferDesc *ssd_buf_hdr;
	long		i, NNonEmpty;

	BandDescForMaxColdNow *band_hdr_for_maxcold_now;
	band_hdr_for_maxcold_now = band_descriptors_for_maxcold_now;
	for (i = 0; i < NSMRBands; band_hdr_for_maxcold_now++, i++) {
		band_hdr_for_maxcold_now->ischosen = 0;
	}

	ssd_buf_hdr = ssd_buffer_descriptors;
    printf("ssd_buffer_strategy_control->n_usedssd=%ld\n", ssd_buffer_strategy_control->n_usedssd);
    printf("ssd_buffer_strategy_control_for_maxcold_history->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_maxcold_history->n_usedssds);
    printf("ssd_buffer_strategy_control_for_maxcold_now->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);
    printf("ssd_buffer_strategy_control_for_lru_read->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_lru_read->n_usedssds);
	for (i = 0; i < ssd_buffer_strategy_control->n_usedssd; i++, ssd_buf_hdr++) {
		if ((ssd_buf_hdr->ssd_buf_flag & SSD_BUF_DIRTY) != 0) {
	        band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr->ssd_buf_tag.offset);
		    band_descriptors_for_maxcold_history[band_num].current_pages++;
        }
	}

	ssd_buf_hdr_for_maxcold_history = ssd_buffer_descriptors_for_maxcold_history;
	for (i = 0; i < ssd_buffer_strategy_control_for_maxcold_history->n_usedssds; i++, ssd_buf_hdr_for_maxcold_history++) {
		if ((ssd_buf_hdr_for_maxcold_history->ssd_buf_flag & SSD_BUF_DIRTY) != 0) {
            band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr_for_maxcold_history->ssd_buf_tag.offset);
            band_descriptors_for_maxcold_history[band_num].current_hits += ssd_buf_hdr_for_maxcold_history->hit_times;
        }
	}

	BandDescForMaxColdHistory *band_hdr_for_maxcold_history;
	band_hdr_for_maxcold_history = band_descriptors_for_maxcold_history;
	for (i = 0; i < NSMRBands; band_hdr_for_maxcold_history++, i++) {
        if (band_hdr_for_maxcold_history->current_pages > 0)
    		band_hdr_for_maxcold_history->to_sort = - band_hdr_for_maxcold_history->current_hits * 1000 / band_hdr_for_maxcold_history->current_pages * 1000 / band_hdr_for_maxcold_history->current_pages;
        else
            band_hdr_for_maxcold_history->to_sort = 0;
	}
    
    NNonEmpty = find_non_empty();
    qsort_band_history(0, NNonEmpty - 1);

	i = 0;
	unsigned long	total = 0;
	while ((i < NSMRBands) && (total< PERIODTIMES || i < NCOLDBAND)) {
		total += band_descriptors_for_maxcold_history[i].current_pages;
		band_descriptors_for_maxcold_now[band_descriptors_for_maxcold_history[i].band_num].ischosen = 1;
		i++;
	}

	ssd_buf_hdr = ssd_buffer_descriptors;
	for (i = 0; i < ssd_buffer_strategy_control->n_usedssd; i++, ssd_buf_hdr++) {
		if ((ssd_buf_hdr->ssd_buf_flag & SSD_BUF_DIRTY) != 0) {
	        band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr->ssd_buf_tag.offset);
		    if (band_descriptors_for_maxcold_now[band_num].ischosen > 0) 
                ssd_buf_hdr->ssd_buf_flag |= SSD_BUF_ISCHOSEN;
            else
                ssd_buf_hdr->ssd_buf_flag &= ~SSD_BUF_ISCHOSEN;
        } else {
            ssd_buf_hdr->ssd_buf_flag |= SSD_BUF_ISCHOSEN;
        }
	}

	resetSSDBufferForMaxColdHistory();
	resetSSDBufferForMaxColdNow();
	run_times = 0;

	return NULL;
}

static volatile void *
addToLRUHead(SSDBufferDescForLRURead * ssd_buf_hdr_for_lru_read)
{
//    printf("in addToLRUHead(): ssd_buffer_strategy_control_for_lru_read->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_lru_read->n_usedssds);
	if (ssd_buffer_strategy_control_for_lru_read->n_usedssds == 0) {
		ssd_buffer_strategy_control_for_lru_read->first_lru = ssd_buf_hdr_for_lru_read->ssd_buf_id;
		ssd_buffer_strategy_control_for_lru_read->last_lru = ssd_buf_hdr_for_lru_read->ssd_buf_id;
	} else {
		ssd_buf_hdr_for_lru_read->next_lru = ssd_buffer_descriptors_for_lru_read[ssd_buffer_strategy_control_for_lru_read->first_lru].ssd_buf_id;
		ssd_buf_hdr_for_lru_read->last_lru = -1;
		ssd_buffer_descriptors_for_lru_read[ssd_buffer_strategy_control_for_lru_read->first_lru].last_lru = ssd_buf_hdr_for_lru_read->ssd_buf_id;
		ssd_buffer_strategy_control_for_lru_read->first_lru = ssd_buf_hdr_for_lru_read->ssd_buf_id;
	}

	return NULL;
}

static volatile void *
deleteFromLRU(SSDBufferDescForLRURead * ssd_buf_hdr_for_lru_read)
{
//    printf("in deleteFromLRU(): ssd_buffer_strategy_control_for_lru_read->n_usedssds=%ld\n", ssd_buffer_strategy_control_for_lru_read->n_usedssds);
	if (ssd_buf_hdr_for_lru_read->last_lru >= 0) {
		ssd_buffer_descriptors_for_lru_read[ssd_buf_hdr_for_lru_read->last_lru].next_lru = ssd_buf_hdr_for_lru_read->next_lru;
	} else {
		ssd_buffer_strategy_control_for_lru_read->first_lru = ssd_buf_hdr_for_lru_read->next_lru;
	}
	if (ssd_buf_hdr_for_lru_read->next_lru >= 0) {
		ssd_buffer_descriptors_for_lru_read[ssd_buf_hdr_for_lru_read->next_lru].last_lru = ssd_buf_hdr_for_lru_read->last_lru;
	} else {
		ssd_buffer_strategy_control_for_lru_read->last_lru = ssd_buf_hdr_for_lru_read->last_lru;
	}

	return NULL;
}

static volatile void *
moveToLRUHead(SSDBufferDescForLRURead * ssd_buf_hdr_for_lru_read)
{
	deleteFromLRU(ssd_buf_hdr_for_lru_read);
	addToLRUHead(ssd_buf_hdr_for_lru_read);

	return NULL;
}

SSDBufferDesc  *
getMaxColdBufferSplitRW(SSDBufferTag new_ssd_buf_tag, bool iswrite)
{
    if (iswrite > 0)
        return getMaxColdBufferWrite(new_ssd_buf_tag);
    else
        return getMaxColdBufferRead(new_ssd_buf_tag);
}

static SSDBufferDesc  *
getMaxColdBufferWrite(SSDBufferTag new_ssd_buf_tag)
{
//    printf("in getMaxColdBufferWrite(), first_freessd=%ld, run_tims=%ld\n", ssd_buffer_strategy_control->first_freessd, run_times);
	if (ssd_buffer_strategy_control->first_freessd < 0 && run_times >= PERIODTIMES)
        pause_and_caculate_next_period_hotdivsize();
	
	SSDBufferDesc  *ssd_buf_hdr;
	SSDBufferDescForMaxColdHistory *ssd_buf_hdr_for_maxcold_history;
	SSDBufferDescForMaxColdNow *ssd_buf_hdr_for_maxcold_now;

	SSDBufferTag	ssd_buf_tag_history = new_ssd_buf_tag;
	unsigned long	ssd_buf_hash_history = ssdbuftableHashcodeHistory(&ssd_buf_tag_history);
	long		ssd_buf_id_history = ssdbuftableLookupHistory(&ssd_buf_tag_history, ssd_buf_hash_history);
	if (ssd_buf_id_history >= 0) {
		ssd_buf_hdr_for_maxcold_history = &ssd_buffer_descriptors_for_maxcold_history[ssd_buf_id_history];
        ssd_buf_hdr_for_maxcold_history->ssd_buf_flag |= SSD_BUF_DIRTY;
		moveToLRUHeadHistory(ssd_buf_hdr_for_maxcold_history);
	} else {
        // we make sure that the condition below is always true
        if (ssd_buffer_strategy_control_for_maxcold_history->first_freessd >= 0) {
            ssd_buf_hdr_for_maxcold_history = &ssd_buffer_descriptors_for_maxcold_history[ssd_buffer_strategy_control_for_maxcold_history->first_freessd];
            ssd_buffer_strategy_control_for_maxcold_history->first_freessd = ssd_buf_hdr_for_maxcold_history->next_freessd;
            ssd_buf_hdr_for_maxcold_history->next_freessd = -1;
        } else
            printf("can not get here2\n");
        ssd_buf_hdr_for_maxcold_history->ssd_buf_tag = new_ssd_buf_tag;
        ssd_buf_hdr_for_maxcold_history->ssd_buf_flag |= SSD_BUF_DIRTY;
		ssdbuftableInsertHistory(&ssd_buf_tag_history, ssd_buf_hash_history, ssd_buf_hdr_for_maxcold_history->ssd_buf_id);
		addToLRUHeadHistory(ssd_buf_hdr_for_maxcold_history);
	}
	ssd_buf_hdr_for_maxcold_history->hit_times++;

	run_times++;

	if (ssd_buffer_strategy_control->first_freessd >= 0) {
		ssd_buf_hdr = &ssd_buffer_descriptors[ssd_buffer_strategy_control->first_freessd];
		ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[ssd_buffer_strategy_control->first_freessd];
		ssd_buffer_strategy_control->first_freessd = ssd_buf_hdr->next_freessd;
		ssd_buf_hdr->next_freessd = -1;

		unsigned long	band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr->ssd_buf_tag.offset);
        if (band_descriptors_for_maxcold_now[band_num].ischosen > 0) { 
			addToLRUHeadNow(ssd_buf_hdr_for_maxcold_now);
		} else
            printf("*********band_num=%ld", band_num);
		ssd_buffer_strategy_control->n_usedssd++;
//        if (ssd_buffer_strategy_control->n_usedssd != ssd_buffer_strategy_control_for_maxcold_now->n_usedssds)
//            printf("================n_usedssd=%ld, now_n_usedssds=%ld\n", ssd_buffer_strategy_control->n_usedssd, ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);
		return ssd_buf_hdr;
	}
	flush_fifo_times++;

	ssd_buf_hdr = &ssd_buffer_descriptors[ssd_buffer_strategy_control_for_maxcold_now->last_lru];
	ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[ssd_buffer_strategy_control_for_maxcold_now->last_lru];

	unsigned long	band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr->ssd_buf_tag.offset);
    if ((ssd_buf_hdr->ssd_buf_flag & SSD_BUF_ISCHOSEN) > 0) {
		deleteFromLRUNow(ssd_buf_hdr_for_maxcold_now);
	} 
	band_num = GetSMRZoneNumFromSSD(new_ssd_buf_tag.offset);
    if (band_descriptors_for_maxcold_now[band_num].ischosen > 0) { 
		addToLRUHeadNow(ssd_buf_hdr_for_maxcold_now);
	}
//        if (ssd_buffer_strategy_control->n_usedssd != ssd_buffer_strategy_control_for_maxcold_now->n_usedssds)
//            printf("0-1================n_usedssd=%ld, now_n_usedssds=%ld\n", ssd_buffer_strategy_control->n_usedssd, ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);
	unsigned char	old_flag = ssd_buf_hdr->ssd_buf_flag;
	SSDBufferTag	old_tag = ssd_buf_hdr->ssd_buf_tag;
	if (DEBUG)
		printf("[INFO] getMaxColdBufferWrite(): old_flag&SSD_BUF_DIRTY=%d\n", old_flag & SSD_BUF_DIRTY);
	if ((old_flag & SSD_BUF_DIRTY) != 0) {
		flushSSDBuffer(ssd_buf_hdr);
	}
	if ((old_flag & SSD_BUF_VALID) != 0) {
		unsigned long	old_hash = ssdbuftableHashcode(&old_tag);
		ssdbuftableDelete(&old_tag, old_hash);
	}

    return ssd_buf_hdr;
}

static SSDBufferDesc  *
getMaxColdBufferRead(SSDBufferTag new_ssd_buf_tag)
{
	SSDBufferDesc  *ssd_buf_hdr;
	SSDBufferDescForLRURead *ssd_buf_hdr_for_lru_read;

	if (ssd_buffer_strategy_control_for_lru_read->first_freessd >= 0) {
		ssd_buf_hdr = &ssd_buffer_descriptors[ssd_buffer_strategy_control_for_lru_read->first_freessd];
		ssd_buf_hdr_for_lru_read = &ssd_buffer_descriptors_for_lru_read[ssd_buffer_strategy_control_for_lru_read->first_freessd];
		ssd_buffer_strategy_control_for_lru_read->first_freessd = ssd_buf_hdr->next_freessd;
		ssd_buf_hdr->next_freessd = -1;
		addToLRUHead(ssd_buf_hdr_for_lru_read);
		ssd_buffer_strategy_control_for_lru_read->n_usedssds++;
		return ssd_buf_hdr;
	}
	ssd_buf_hdr = &ssd_buffer_descriptors[ssd_buffer_strategy_control_for_lru_read->last_lru];
	ssd_buf_hdr_for_lru_read = &ssd_buffer_descriptors_for_lru_read[ssd_buffer_strategy_control_for_lru_read->last_lru];
	moveToLRUHead(ssd_buf_hdr_for_lru_read);
	unsigned char	old_flag = ssd_buf_hdr->ssd_buf_flag;
	SSDBufferTag	old_tag = ssd_buf_hdr->ssd_buf_tag;
	if (DEBUG)
		printf("[INFO] getMaxColdBufferRead(): old_flag&SSD_BUF_DIRTY=%d\n", old_flag & SSD_BUF_DIRTY);
	if ((old_flag & SSD_BUF_VALID) != 0) {
		unsigned long	old_hash = ssdbuftableHashcode(&old_tag);
		ssdbuftableDelete(&old_tag, old_hash);
	}
	return ssd_buf_hdr;
}

void           *
hitInMaxColdBufferSplitRW(SSDBufferDesc * ssd_buf_hdr, bool iswrite)
{
    if (iswrite > 0)
        hitInMaxColdBufferWrite(ssd_buf_hdr);
    else
        hitInMaxColdBufferRead(ssd_buf_hdr);
}

static void           *
hitInMaxColdBufferWrite(SSDBufferDesc * ssd_buf_hdr)
{
	SSDBufferDescForMaxColdHistory *ssd_buf_hdr_for_maxcold_history;

	SSDBufferTag	ssd_buf_tag_history = ssd_buf_hdr->ssd_buf_tag;
	unsigned long	ssd_buf_hash_history = ssdbuftableHashcodeHistory(&ssd_buf_tag_history);
	long		ssd_buf_id_history = ssdbuftableLookupHistory(&ssd_buf_tag_history, ssd_buf_hash_history);
//        if (ssd_buffer_strategy_control->n_usedssd != ssd_buffer_strategy_control_for_maxcold_now->n_usedssds)
//            printf("1================n_usedssd=%ld, now_n_usedssds=%ld\n", ssd_buffer_strategy_control->n_usedssd, ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);
    if ((ssd_buf_hdr->ssd_buf_flag & SSD_BUF_DIRTY) > 0) {
	    ssd_buf_hdr_for_maxcold_history = &ssd_buffer_descriptors_for_maxcold_history[ssd_buf_id_history];
	    ssd_buf_hdr_for_maxcold_history->hit_times++;

        ssd_buf_hdr_for_maxcold_history->ssd_buf_flag |= SSD_BUF_DIRTY;
	    moveToLRUHeadHistory(&ssd_buffer_descriptors_for_maxcold_history[ssd_buf_hdr_for_maxcold_history->ssd_buf_id]);
	    unsigned long	band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr->ssd_buf_tag.offset);
	    if ((ssd_buf_hdr->ssd_buf_flag & SSD_BUF_ISCHOSEN) > 0) {
	        SSDBufferDescForMaxColdNow *ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[ssd_buf_hdr->ssd_buf_id];
		    deleteFromLRUNow(ssd_buf_hdr_for_maxcold_now);
	    } 
        if (band_descriptors_for_maxcold_now[band_num].ischosen > 0) { 
	        SSDBufferDescForMaxColdNow *ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[ssd_buf_hdr->ssd_buf_id];
		    addToLRUHeadNow(ssd_buf_hdr_for_maxcold_now);
//        if (ssd_buffer_strategy_control->n_usedssd != ssd_buffer_strategy_control_for_maxcold_now->n_usedssds)
//            printf("2================n_usedssd=%ld, now_n_usedssds=%ld\n", ssd_buffer_strategy_control->n_usedssd, ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);
	    }
    } else {
        /* write hit in read buffer
         * 1. add its metadata in write buffer
         * 2. delete its metadata in read buffer
         * 3. change ssd_buf_hdr to point to buffer header in write buffer
         */
        // 1. add its metadata in write buffer
	    SSDBufferDesc  *old_ssd_buf_hdr;
	    SSDBufferDescForMaxColdHistory *ssd_buf_hdr_for_maxcold_history;
	    SSDBufferDescForMaxColdNow *ssd_buf_hdr_for_maxcold_now;

        // 1.1 add to ssd_buffer_strategy_control_for_maxcold_history
        // we make sure that the condition below is always true
        if (ssd_buffer_strategy_control_for_maxcold_history->first_freessd >= 0) {
            ssd_buf_hdr_for_maxcold_history = &ssd_buffer_descriptors_for_maxcold_history[ssd_buffer_strategy_control_for_maxcold_history->first_freessd];
            ssd_buffer_strategy_control_for_maxcold_history->first_freessd = ssd_buf_hdr_for_maxcold_history->next_freessd;
            ssd_buf_hdr_for_maxcold_history->next_freessd = -1;
        } else 
            printf("can not get here1\n");
        ssd_buf_hdr_for_maxcold_history->ssd_buf_tag = ssd_buf_hdr->ssd_buf_tag;
        ssd_buf_hdr_for_maxcold_history->ssd_buf_flag |= SSD_BUF_DIRTY;
		ssdbuftableInsertHistory(&ssd_buf_tag_history, ssd_buf_hash_history, ssd_buf_hdr_for_maxcold_history->ssd_buf_id);
		addToLRUHeadHistory(ssd_buf_hdr_for_maxcold_history);
	    ssd_buf_hdr_for_maxcold_history->hit_times++;

        // 1.2 add to ssd_buffer_strategy_control_for_maxcold_now
	    if (ssd_buffer_strategy_control->first_freessd >= 0) {
		    old_ssd_buf_hdr = &ssd_buffer_descriptors[ssd_buffer_strategy_control->first_freessd];
		    ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[ssd_buffer_strategy_control->first_freessd];
		    ssd_buffer_strategy_control->first_freessd = old_ssd_buf_hdr->next_freessd;
		    old_ssd_buf_hdr->next_freessd = -1;

		    unsigned long	band_num = GetSMRZoneNumFromSSD(old_ssd_buf_hdr->ssd_buf_tag.offset);
            if (band_descriptors_for_maxcold_now[band_num].ischosen > 0) { 
			    addToLRUHeadNow(ssd_buf_hdr_for_maxcold_now);
		    }
		    ssd_buffer_strategy_control->n_usedssd++;
//        if (ssd_buffer_strategy_control->n_usedssd != ssd_buffer_strategy_control_for_maxcold_now->n_usedssds)
//            printf("3-1================n_usedssd=%ld, now_n_usedssds=%ld\n", ssd_buffer_strategy_control->n_usedssd, ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);
		    return NULL;
	    } else {
	        if (ssd_buffer_strategy_control->first_freessd < 0 && run_times >= PERIODTIMES)
                pause_and_caculate_next_period_hotdivsize();
	        flush_fifo_times++;
            run_times ++;

	        old_ssd_buf_hdr = &ssd_buffer_descriptors[ssd_buffer_strategy_control_for_maxcold_now->last_lru];
	        ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[ssd_buffer_strategy_control_for_maxcold_now->last_lru];

	        unsigned long	band_num = GetSMRZoneNumFromSSD(old_ssd_buf_hdr->ssd_buf_tag.offset);
	        if ((old_ssd_buf_hdr->ssd_buf_flag & SSD_BUF_ISCHOSEN) > 0) {
	            SSDBufferDescForMaxColdNow *ssd_buf_hdr_for_maxcold_now = &ssd_buffer_descriptors_for_maxcold_now[old_ssd_buf_hdr->ssd_buf_id];
		        deleteFromLRUNow(ssd_buf_hdr_for_maxcold_now);
            }
	        band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr->ssd_buf_tag.offset);
            if (band_descriptors_for_maxcold_now[band_num].ischosen > 0) { 
		        addToLRUHeadNow(ssd_buf_hdr_for_maxcold_now);
	        }
//        if (ssd_buffer_strategy_control->n_usedssd != ssd_buffer_strategy_control_for_maxcold_now->n_usedssds)
//            printf("3-2================n_usedssd=%ld, now_n_usedssds=%ld\n", ssd_buffer_strategy_control->n_usedssd, ssd_buffer_strategy_control_for_maxcold_now->n_usedssds);
	        unsigned char	old_flag = old_ssd_buf_hdr->ssd_buf_flag;
	        SSDBufferTag	old_tag = old_ssd_buf_hdr->ssd_buf_tag;
	        if (DEBUG)
		        printf("[INFO] SSDBufferAlloc(): old_flag&SSD_BUF_DIRTY=%d\n", old_flag & SSD_BUF_DIRTY);
	        if ((old_flag & SSD_BUF_DIRTY) != 0) {
		        flushSSDBuffer(old_ssd_buf_hdr);
	        }
	        if ((old_flag & SSD_BUF_VALID) != 0) {
		        unsigned long	old_hash = ssdbuftableHashcode(&old_tag);
		        ssdbuftableDelete(&old_tag, old_hash);
	        }
        }
        
        // 2. delete its metadata in read buffer
        SSDBufferDescForLRURead *ssd_buf_hdr_for_lru_read = &ssd_buffer_descriptors_for_lru_read[ssd_buf_hdr->ssd_buf_id];
        deleteFromLRU(ssd_buf_hdr_for_lru_read);
        if ((ssd_buf_hdr->ssd_buf_flag & SSD_BUF_VALID) != 0) {
            unsigned long   old_hash = ssdbuftableHashcode(&ssd_buf_hdr->ssd_buf_tag);
            ssdbuftableDelete(&ssd_buf_hdr->ssd_buf_tag, old_hash);
        }
        ssd_buffer_strategy_control_for_lru_read->n_usedssds --;
        ssd_buf_hdr->ssd_buf_flag = 0;
        ssd_buf_hdr->next_freessd = ssd_buffer_strategy_control_for_lru_read->first_freessd;
        ssd_buffer_strategy_control_for_lru_read->first_freessd = ssd_buf_hdr->ssd_buf_id;
        ssd_buffer_descriptors_for_lru_read[ssd_buf_hdr->ssd_buf_id].next_lru = -1;
        ssd_buffer_descriptors_for_lru_read[ssd_buf_hdr->ssd_buf_id].last_lru = -1;

        // 3. change ssd_buf_hdr to point to buffer header in write buffer
        ssd_buf_hdr = old_ssd_buf_hdr;
    }
	return NULL;
}

static void           *
hitInMaxColdBufferRead(SSDBufferDesc * ssd_buf_hdr)
{
    if ((ssd_buf_hdr->ssd_buf_flag & SSD_BUF_DIRTY) > 0) {
        hitInMaxColdBufferWrite(ssd_buf_hdr);
    } else
	    moveToLRUHead(&ssd_buffer_descriptors_for_lru_read[ssd_buf_hdr->ssd_buf_id]);

	return NULL;
}

bool
isOpenForEvictedSplitRW(SSDBufferDesc * ssd_buf_hdr)
{
	unsigned long	band_num = GetSMRZoneNumFromSSD(ssd_buf_hdr->ssd_buf_tag.offset);
    return band_descriptors_for_maxcold_now[band_num].ischosen;
}
