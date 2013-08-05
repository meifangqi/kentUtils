# tilingPath.sql was originally generated by the autoSql program, which also 
# generated tilingPath.c and tilingPath.h.  This creates the database representation of
# an object which can be loaded and saved from RAM in a fairly 
# automatic way.

#A tiling path of clones through a chromosome
CREATE TABLE tilingPath (
    chrom varchar(255) not null,	# Chromosome name: chr1, chr2, etc.
    accession varchar(255) not null,	# Clone accession or ? or GAP
    clone varchar(255) not null,	# Clone name in BAC library
    contig varchar(255) not null,	# Contig (or gap size)
    chromIx int not null,	# Number of clone in tiling path starting chrom start
              #Indices
    PRIMARY KEY(chrom)
);