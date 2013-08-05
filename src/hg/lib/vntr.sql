# vntr.sql was originally generated by the autoSql program, which also 
# generated vntr.c and vntr.h.  This creates the database representation of
# an object which can be loaded and saved from RAM in a fairly 
# automatic way.

#Microsatellites from Gerome Breen's VNTR program (bed 4+)
CREATE TABLE vntr (
    bin smallint not null,		# bin for speed
    chrom varchar(255) not null,	# chrom
    chromStart int unsigned not null,	# Start position in chromosome
    chromEnd int unsigned not null,	# End position in chromosome
    name varchar(255) not null,	# Name of item (Repeat unit)
    repeatCount float not null,	# Number of perfect repeats
    distanceToLast int not null,	# Distance to previous microsat. repeat
    distanceToNext int not null,	# Distance to next microsat. repeat
    forwardPrimer varchar(255) not null,	# Forward PCR primer sequence (or Design_Failed)
    reversePrimer varchar(255) not null,	# Reverse PCR primer sequence (or Design_Failed)
    pcrLength varchar(255) not null,	# PCR product length (or Design_Failed)
              #Indices
    INDEX(chrom(8),bin),
    INDEX(name(8))
);