/* bigWigAverageOverBed - Compute average score of big wig over each bed, which may have introns. */

/* Copyright (C) 2014 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "localmem.h"
#include "options.h"
#include "verbose.h"
#include "basicBed.h"
#include "bigWig.h"
#include "bits.h"


char *bedOut = NULL;
char *statsRa = NULL;
int sampleAroundCenter = 0;

void usage()
/* Explain usage and exit. */
{
errAbort(
  "bigWigAverageOverBed v2 - Compute average score of big wig over each bed, which may have introns.\n"
  "usage:\n"
  "   bigWigAverageOverBed in.bw in.bed out.tab\n"
  "The output columns are:\n"
  "   name - name field from bed, which should be unique\n"
  "   size - size of bed (sum of exon sizes\n"
  "   covered - # bases within exons covered by bigWig\n"
  "   sum - sum of values over all bases covered\n"
  "   mean0 - average over bases with non-covered bases counting as zeroes\n"
  "   mean - average over just covered bases\n"
  "Options:\n"
  "   -stats=stats.ra - Output a collection of overall statistics to stat.ra file\n"
  "   -bedOut=out.bed - Make output bed that is echo of input bed but with mean column appended\n"
  "   -sampleAroundCenter=N - Take sample at region N bases wide centered around bed item, rather\n"
  "                     than the usual sample in the bed item.\n"
  );
}

static struct optionSpec options[] = {
   {"bedOut", OPTION_STRING},
   {"stats", OPTION_STRING},
   {"sampleAroundCenter", OPTION_INT},
   {NULL, 0},
};

void checkUniqueNames(struct bed *bedList)
/* Make sure all names in bedList are unique */
{
struct hash *hash = hashNew(16);
struct bed *bed;
for (bed = bedList; bed != NULL; bed = bed->next)
    {
    char *name = bed->name;
    if (hashLookup(hash, name) != NULL)
        errAbort("%s duplicated in input bed", name);
    else
        hashAdd(hash, name, NULL);
    }
hashFree(&hash);
}

void addBigWigIntervalInfo(struct bbiFile *bbi, struct lm *lm, char *chrom, int start, int end,
    int *pSumSize, int *pSumCoverage, double *pSumVal)
/* Read in interval from bigBed and add it sums. */
{
struct bbiInterval *iv, *ivList = bigWigIntervalQuery(bbi, chrom, start, end, lm);
*pSumSize += (end - start);
for (iv = ivList; iv != NULL; iv = iv->next)
    {
    int cov1 = rangeIntersection(iv->start, iv->end, start, end);
    if (cov1 > 0)
	{
	*pSumCoverage += cov1;
	*pSumVal += cov1 * iv->val;
	}
    }
}
	
int countBlocks(struct bed *bedList, int fieldCount)
/* Return the number of blocks in list, or if non-blocked beds, just number of beds. */
{
if (fieldCount < 12)
    return slCount(bedList);
int blockCount = 0;
struct bed *bed;
for (bed = bedList; bed != NULL; bed = bed->next)
    blockCount += bed->blockCount;
return blockCount;
}

void optionallyPrintBedPlus(FILE *f, struct bed *bed, int fieldCount, double extra)
/* Print BED to tab separated file plus an extra double-format column. */
{
if (f != NULL)
    {
    bedOutputN(bed, fieldCount, f, '\t', '\t');
    fprintf(f, "%g\n", extra);
    }
}

double sumSum;
long long sumCoverage;
long long sumSize;

void updateSums(double sum, int coverage, int size)
/* Just add to the above three numbers. */
{
sumSum += sum;
sumCoverage += coverage;
sumSize += size;
}

long long bbiTotalChromSize(struct bbiFile *bbi)
/* Return sum of sizes of all chromosomes */
{
struct bbiChromInfo *chrom, *chromList = bbiChromList(bbi);
long long total = 0;
for (chrom = chromList; chrom != NULL; chrom = chrom->next)
    total += chrom->size;
bbiChromInfoFreeList(&chromList);
return total;
}

/* Return all chromosomes in file.  Dispose of this with bbiChromInfoFreeList. */
void outputSums(char *fileName, struct bbiFile *bbi)
/* Write a little .ra file with results of sums. */
{
FILE *f = mustOpen(fileName, "w");
struct bbiSummaryElement sumEl = bbiTotalSummary(bbi);
double totalSignal = sumEl.sumData;
long long basesInGenome = bbiTotalChromSize(bbi);
fprintf(f, "spotRatio %g\n", sumSum/totalSignal);
fprintf(f, "enrichment %g\n", (sumSum/sumSize) / (totalSignal/basesInGenome));
fprintf(f, "maxEnrichment %g\n", (double)basesInGenome/sumSize);
fprintf(f, "basesInGenome %lld\n", basesInGenome);
fprintf(f, "basesInSpots %lld\n", sumSize);
fprintf(f, "basesInSpotsWithSignal %lld\n", sumCoverage);
fprintf(f, "sumSignal %g\n", totalSignal);
fprintf(f, "spotSumSignal %g\n", sumSum);
carefulClose(&f);
}


void averageFetchingEachBlock(struct bbiFile *bbi, struct bed *bedList, int fieldCount, 
	FILE *f, FILE *bedF)
/* Do the averaging fetching each block from bedList from bigWig.  Fastest for short bedList. */
{
struct lm *lm = lmInit(0);
struct bed *bed;
for (bed = bedList; bed != NULL; bed = bed->next)
    {
    int coverage = 0;
    double sum = 0.0;
    int size = 0;

    if (sampleAroundCenter > 0)
        {
	int center = (bed->chromStart + bed->chromEnd)/2;
	int left = center - (sampleAroundCenter/2);
	addBigWigIntervalInfo(bbi, lm, bed->chrom, left, left+sampleAroundCenter, 
		&size, &coverage, &sum);
	}
    else
	{
	if (fieldCount < 12)
	    addBigWigIntervalInfo(bbi, lm, bed->chrom, bed->chromStart, bed->chromEnd, 
		    &size, &coverage, &sum);
	else
	    {
	    int i;
	    for (i=0; i<bed->blockCount; ++i)
		{
		int start = bed->chromStart + bed->chromStarts[i];
		int end = start + bed->blockSizes[i];
		addBigWigIntervalInfo(bbi, lm, bed->chrom, start, end, &size, &coverage, &sum);
		}
	    }
	}

    /* Print out result, fudging mean to 0 if no coverage at all. */
    double mean = 0;
    if (coverage > 0)
	 mean = sum/coverage;
    fprintf(f, "%s\t%d\t%d\t%g\t%g\t%g\n", bed->name, size, coverage, sum, sum/size, mean);
    optionallyPrintBedPlus(bedF, bed, fieldCount, mean);
    updateSums(sum, coverage, size);
    }
}

int bedCmpChrom(const void *va, const void *vb)
/* Compare strings such as chromosome names that may have embedded numbers,
 * so that chr4 comes before chr14 */
{
const struct bed *a = *((struct bed **)va);
const struct bed *b = *((struct bed **)vb);
return cmpStringsWithEmbeddedNumbers(a->chrom, b->chrom);
}

struct bed *nextChromInList(struct bed *bedList)
/* Return first bed in list that starts with another chromosome, or NULL if none. */
{
char *chrom = bedList->chrom;
struct bed *bed;
for (bed = bedList->next; bed != NULL; bed = bed->next)
    if (!sameString(bed->chrom, chrom))
        break;
return bed;
}

void addBufIntervalInfo(double *valBuf, Bits *covBuf, int start, int end,
    int *pSumSize, int *pSumCoverage, double *pSumVal)
/* Look at interval in buffers and add result to sums. */
{
int size1 = end - start;
*pSumSize += size1;
int cov1 = bitCountRange(covBuf, start, size1);
*pSumCoverage += cov1;
int i;
double sum1 = 0;
for (i=start; i<end; ++i)
    sum1 += valBuf[i];
*pSumVal += sum1;
}

void averageFetchingEachChrom(struct bbiFile *bbi, struct bed **pBedList, int fieldCount, 
	FILE *f, FILE *bedF)
/* Do the averaging by sorting bedList by chromosome, and then processing each chromosome
 * at once. Faster for long bedLists. */
{
/* Sort by chromosome. */
slSort(pBedList, bedCmpChrom);

struct bigWigValsOnChrom *chromVals = bigWigValsOnChromNew();

struct bed *bed, *bedList, *nextChrom;
verbose(1, "processing chromosomes");
for (bedList = *pBedList; bedList != NULL; bedList = nextChrom)
    {
    /* Figure out which chromosome we're working on, and the last bed using it. */
    char *chrom = bedList->chrom;
    nextChrom = nextChromInList(bedList);
    verbose(2, "Processing %s\n", chrom);

    if (bigWigValsOnChromFetchData(chromVals, chrom, bbi))
	{
	double *valBuf = chromVals->valBuf;
	Bits *covBuf = chromVals->covBuf;

	/* Loop through beds doing sums and outputting. */
	for (bed = bedList; bed != nextChrom; bed = bed->next)
	    {
	    int size = 0, coverage = 0;
	    double sum = 0.0;
	    if (sampleAroundCenter > 0)
		{
		int center = (bed->chromStart + bed->chromEnd)/2;
		int left = center - (sampleAroundCenter/2);
		addBufIntervalInfo(valBuf, covBuf, left, left+sampleAroundCenter,
		    &size, &coverage, &sum);
		}
	    else
		{
		if (fieldCount < 12)
		    {
		    addBufIntervalInfo(valBuf, covBuf, bed->chromStart, bed->chromEnd,
			&size, &coverage, &sum);
		    }
		else
		    {
		    int i;
		    for (i=0; i<bed->blockCount; ++i)
			{
			int start = bed->chromStart + bed->chromStarts[i];
			int end = start + bed->blockSizes[i];
			addBufIntervalInfo(valBuf, covBuf, start, end, &size, &coverage, &sum);
			}
		    }
		}

	    /* Print out result, fudging mean to 0 if no coverage at all. */
	    double mean = 0;
	    if (coverage > 0)
		 mean = sum/coverage;
	    fprintf(f, "%s\t%d\t%d\t%g\t%g\t%g\n", bed->name, size, coverage, sum, sum/size, mean);
	    optionallyPrintBedPlus(bedF, bed, fieldCount, mean);
	    updateSums(sum, coverage, size);
	    }
	verboseDot();
	}
    else
        {
	/* If no bigWig data on this chromosome, just output as if coverage is 0 */
	for (bed = bedList; bed != nextChrom; bed = bed->next)
	    {
	    fprintf(f, "%s\t%d\t0\t0\t0\t0\n", bed->name, bedTotalBlockSize(bed));
	    optionallyPrintBedPlus(bedF, bed, fieldCount, 0);
	    }
	}
    }
verbose(1, "\n");
}

void bigWigAverageOverBed(char *inBw, char *inBed, char *outTab)
/* bigWigAverageOverBed - Compute average score of big wig over each bed, which may have introns. */
{
struct bed *bedList;
int fieldCount;
bedLoadAllReturnFieldCount(inBed, &bedList, &fieldCount);
checkUniqueNames(bedList);

struct bbiFile *bbi = bigWigFileOpen(inBw);
FILE *f = mustOpen(outTab, "w");
FILE *bedF = NULL;
if (bedOut != NULL)
    bedF = mustOpen(bedOut, "w");

/* Count up number of blocks in file.  It takes about 1/100th of of second to
 * look up a single block in a bigWig.  On the other hand to stream through
 * the whole file setting a array of doubles takes about 30 seconds, so we change
 * strategy at 3,000 blocks. 
 *   I (Jim) usually avoid having two paths through the code like this, and am tempted
 * to always go the ~30 second chromosome-at-a-time  way.  On the other hand the block-way
 * was developed first, and it was useful to have both ways to test against each other.
 * (This found a bug where the chromosome way wasn't handling beds in chromosomes not
 * covered by the bigWig for instance).  Since this code is not likely to change too
 * much, keeping both implementations in seems reasonable. */
int blockCount = countBlocks(bedList, fieldCount);
verbose(2, "Got %d blocks, if >= 3000 will use chromosome-at-a-time method\n", blockCount);

if (blockCount < 3000)
    averageFetchingEachBlock(bbi, bedList, fieldCount, f, bedF);
else
    averageFetchingEachChrom(bbi, &bedList, fieldCount, f, bedF);
if (statsRa != NULL)
    outputSums(statsRa, bbi);

carefulClose(&bedF);
carefulClose(&f);
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 4)
    usage();
bedOut = optionVal("bedOut", bedOut);
statsRa = optionVal("stats", statsRa);
sampleAroundCenter = optionInt("sampleAroundCenter", sampleAroundCenter);
bigWigAverageOverBed(argv[1], argv[2], argv[3]);
return 0;
}
