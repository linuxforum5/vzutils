#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <math.h>
#include <string.h>

unsigned char verbose = 0;
unsigned char cassetteOnly = 0; 
char* defaultExtension = "wav";
int bytes = 0;
int preamble = 128;

/**************************************************************************************************************************************************
 * from vz2cas.c
 **************************************************************************************************************************************************/
struct vz {
    char vz_magic[4];
    char vz_name[17];
    unsigned char vz_type;
    uint16_t vz_start;
};

/**************************************************************************************************************************************************
 * Wav methodes
 **************************************************************************************************************************************************/
const unsigned int defaultBaud = 22050; // 44100; // 8000; // max 13000
const unsigned char SILENCE = 0x7F;
const unsigned char POS_PEAK = 0xFF; // 8;
const unsigned char NEG_PEAK = 0x00; // 8;
unsigned char silence = SILENCE;
unsigned char high = POS_PEAK;
unsigned char low = NEG_PEAK;
// 48000 esetén 20,833333333us egy impulzus
int half_wave_length_us = 277;
double prefix_silence_length_s = 1; // 1.0632;

/* WAV file header structure */
/* should be 1-byte aligned */
#pragma pack(1)
struct wav_header { // 44 bytes
    char           riff[ 4 ];       // 0.  4 bytes
    uint32_t       rLen;            // 4.  4 bytes
    char           WAVE[ 4 ];       // 8.  4 bytes
    char           fmt[ 4 ];        // 12. 4 bytes
    uint32_t       fLen;            /* 16. 0x1020 */
    uint16_t       wFormatTag;      /* 20. 0x0001 */
    uint16_t       nChannels;       /* 22. 0x0001 */
    uint32_t       nSamplesPerSec;  // 24.
    uint32_t       nAvgBytesPerSec; // 28. nSamplesPerSec*nChannels*(nBitsPerSample/8)
    uint16_t       nBlockAlign;     /* 32. 0x0001 */
    uint16_t       nBitsPerSample;  /* 34. 0x0008 */
    char           datastr[ 4 ];    // 36. 4 bytes
    unsigned int   data_size;       // 40. 4 bytes
} waveHeader = {
    'R','I','F','F', //     Chunk ID - konstans, 4 byte hosszú, értéke 0x52494646, ASCII kódban "RIFF"
    0,               //     Chunk Size - 4 byte hosszú, a fájlméretet tartalmazza bájtokban a fejléccel együtt, értéke 0x01D61A72 (decimálisan 30808690, vagyis a fájl mérete ~30,8 MB)
    'W','A','V','E', //     Format - konstans, 4 byte hosszú,értéke 0x57415645, ASCII kódban "WAVE"
    'f','m','t',' ', //     SubChunk1 ID - konstans, 4 byte hosszú, értéke 0x666D7420, ASCII kódban "fmt "
    16,              //     SubChunk1 Size - 4 byte hosszú, a fejléc méretét tartalmazza, esetünkben 0x00000010
    1,               //     Audio Format - 2 byte hosszú, PCM esetében 0x0001
    1,               //     Num Channels - 2 byte hosszú, csatornák számát tartalmazza, esetünkben 0x0002
    defaultBaud,     //     Sample Rate - 4 byte hosszú, mintavételezési frekvenciát tartalmazza, esetünkben 0x00007D00 (decimálisan 32000)
    defaultBaud,     //     Byte Rate - 4 byte hosszú, értéke 0x0000FA00 (decmálisan 64000)
    1,               //     Block Align - 2 byte hosszú, az 1 mintában található bájtok számát tartalmazza - 0x0002
    8,               //     Bits Per Sample - 2 byte hosszú, felbontást tartalmazza bitekben, értéke 0x0008
    'd','a','t','a', //     Sub Chunk2 ID - konstans, 4 byte hosszú, értéke 0x64617461, ASCII kódban "data"
    0                //     Sub Chunk2 Size - 4 byte hosszú, az adatblokk méretét tartalmazza bájtokban, értéke 0x01D61A1E
};

int write_samples( FILE *wav, long cnt, unsigned char value ) { 
    for( long i = 0; i < cnt; i++ ) fputc( value, wav );
}

int write_pulse( FILE *wav, int us ) {
    int cnt = us * waveHeader.nSamplesPerSec / 1000000;
    write_samples( wav, cnt/2+1, high );
    write_samples( wav, cnt/2, low );
    return 0;
}

int write_bit_into_wav( FILE *wav, unsigned char bit, int d ) {
    d = write_pulse( wav, 2*half_wave_length_us );
    if ( bit ) { // 1 : 
        for ( int i=0; i<2; i++ ) {
            d = write_pulse( wav, 2*half_wave_length_us );
        }
    } else { // 0 :
        d = write_pulse( wav, 4*half_wave_length_us );
    }
    return 0;
}

static void close_wav( FILE *wav ) {
    int full_size = ftell( wav );
    fseek( wav, 4, SEEK_SET );
    fwrite( &full_size, sizeof( full_size ), 1, wav ); // Wave header 2. field : filesize with header. First the lowerest byte

    int data_size = full_size - sizeof( waveHeader );
    fseek( wav, sizeof( waveHeader ) - 4 ,SEEK_SET ); // data chunk size position: 40
    fwrite( &data_size, sizeof( data_size ), 1, wav );
    fclose( wav );
}

unsigned char write_byte_into_wav( FILE* output, unsigned char c ) { // Bitkiírási sorrend: először a 7. bit legvégül a 0. bit
    bytes++;
    if ( cassetteOnly ) {
        fputc( c, output );
    } else {
        int d = 0;
        for( int i = 0; i < 8; i++ ) {
            unsigned char bit = ( c << i ) & 128; // 7. bit a léptetés után. 1. lépés után az eredeti 6. bitje.
            d = write_bit_into_wav( output, bit, d );
        }
    }
    return c;
}

void write_bytes( FILE* output, int counter, unsigned char c ) {
    for( int i=0; i<counter; i++ ) write_byte_into_wav( output, c );
}

static void init_wav( FILE *wavfile ) {
    unsigned int i;
    fwrite( &waveHeader, sizeof( waveHeader ), 1, wavfile );
    /* Lead in silence */
    write_samples( wavfile, prefix_silence_length_s * waveHeader.nSamplesPerSec, silence );
}

static void show_vz_data( struct vz vz ) {
    printf( "Source filetype: %02X\n", vz.vz_type );
}

static uint16_t process_vz_header( FILE *input, FILE* output ) {
    struct vz vz;
    fseek( input, 0, SEEK_END );
    uint16_t datasize = ftell( input ) - sizeof( struct vz );
    fseek( input, 0, SEEK_SET );
    fread( &vz, sizeof( struct vz ), 1, input );
    if ( verbose ) show_vz_data( vz );
    uint16_t end_addr = vz.vz_start + datasize;
    write_bytes( output, preamble, 0x80 );            // preamble
    write_bytes( output, 5, 0xFE );              // leadin
    write_byte_into_wav( output, vz.vz_type );
    for( int i=0; (i<17) && vz.vz_name[i]; i++) write_byte_into_wav( output, vz.vz_name[i] ); // name
    write_byte_into_wav( output, 0 ); // name closer 0
    write_samples( output, 2 * 3 * 2 * half_wave_length_us * waveHeader.nSamplesPerSec / 1000000, low );
    uint16_t checksum = write_byte_into_wav( output, vz.vz_start & 0xff ); // start
    checksum += write_byte_into_wav( output, (vz.vz_start >> 8) & 0xff );
    checksum += write_byte_into_wav( output, end_addr & 0xff );            // end
    checksum += write_byte_into_wav( output, (end_addr >> 8) & 0xff );
    if ( verbose ) printf( "Start address: %04X\n", vz.vz_start );
    if ( verbose ) printf( "End address: %04X\n", end_addr );
    return checksum;
};

static uint16_t process_vz_data( FILE *input, FILE* output ) {
    uint16_t checksum = 0;
    for ( unsigned char c = fgetc( input ); !feof( input ); c = fgetc( input ) ) {
        checksum += write_byte_into_wav( output, c );
    }
    fclose( input );
    return checksum;
}

static void process_vz( FILE *input, FILE* output ) {
    uint16_t checksum = process_vz_header( input, output );
    checksum += process_vz_data( input, output );
    checksum &= 0xffff;
    write_byte_into_wav( output, checksum & 0xff );
    write_byte_into_wav( output, (checksum >> 8) & 0xff );
    for( int i=0; i<20; i++ )  write_byte_into_wav( output, 0 );
    if ( verbose ) {
        printf( "Checksum is %04X\n", checksum );
        printf( "Full size is %d bytes\n", bytes );
    }
}

static void print_usage() {
    printf( "Vz2Wav v1.0\n");
    printf( "VTech VZ/Laser vz file convert to Wav\n" );
    printf( "Copyright 2024 by Laszlo Princz\n" );
    printf( "Usage:\n" );
    printf( "vz2wav [ options ] <input_filename> [ <output_filename> ]\n" );
    printf( "If you don't specify the name of the output file, it will be the input filename with '.wav' extension.\n" );
    printf( "Command line options:\n" );
    printf( "-f <freq>  : Supported sample rates are: 48000, 44100, 22050, 11025 and 8000. Defaut is %dHz\n", defaultBaud );
    printf( "-s <value> : Starting preamble length. [128-255] Default is 0x%02X\n", preamble );
    printf( "-S <value> : Silence value. [0-255] Default is 0x%02X\n", silence );
    printf( "-p <length>: Prefix silence length in seconds. Default value is %.2f\n", prefix_silence_length_s );
    printf( "-H <value> : High value. [0-255] Default is 0x%02X\n", high );
    printf( "-L <value> : Low value. [0-255] Default is 0x%02X\n", low );
    printf( "-i         : Create an inverted wav file", low );
    printf( "-P <us>    : Peak length in us Default is %d\n", half_wave_length_us );
    printf( "-c         : output is binary casette format (.cas) instead of wav\n" );
    printf( "-v         : Verbose output\n" );
    printf( "-h         : prints this text\n" );
    exit(1);
}

char* changeExtensionTo( char *ext, char *filename ) { // Change extension
    int size = strlen( filename ) + strlen( ext ) + 2; // Max new filename length + 1 for 0 character
    char* newFilename = malloc( size );
    int lastDotIndex = 0;
    int i;
    for( i = 0; filename[ i ]; i++ ) {
        if ( filename[ i ] == '.' ) lastDotIndex = i;
        newFilename[ i ] = filename[ i ];
    }
    if ( lastDotIndex ) {
        strcpy( newFilename + lastDotIndex + 1, ext );
    } else { // Ha .-tal kezdődik a neve, vagy nincs benne pont., akkor append
        newFilename[ i++ ] = '.';
        strcpy( newFilename + i, ext );
    }
    return newFilename;
}

int main(int argc, char *argv[]) {
    int finished = 0;
    int arg1, arg2;
    FILE *vz = 0, *output = 0;

    while (!finished) {
        switch (getopt (argc, argv, "?hicvf:H:S:p:L:P:i:o:")) {
            case -1:
            case ':':
                finished = 1;
                break;
            case '?':
            case 'h':
                print_usage();
                break;
            case 'i':
                low = POS_PEAK;
                high = NEG_PEAK;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'c':
                cassetteOnly = 1;
                defaultExtension = "cas";
                break;
            case 'H':
                if ( !sscanf( optarg, "%i", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-H'.\n");
                    exit(2);
                } else {
                    high = arg1;
                }
                break;
            case 'L':
                if ( !sscanf( optarg, "%i", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-L'.\n");
                    exit(2);
                } else {
                    low = arg1;
                }
                break;
            case 'S':
                if ( !sscanf( optarg, "%i", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-S'.\n");
                    exit(2);
                } else {
                    silence = arg1;
                }
                break;
            case 'p':
                if ( !sscanf( optarg, "%lf", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-p'.\n");
                    exit(2);
                } else {
                    prefix_silence_length_s = arg1;
                }
                break;
            case 'P':
                if ( !sscanf( optarg, "%i", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-P'.\n");
                    exit(2);
                } else {
                    half_wave_length_us = arg1;
                }
                break;
            case 'f':
                if ( !sscanf( optarg, "%i", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-f'.\n");
                    exit(2);
                } else {
                    if ( arg1!=48000 && arg1!=44100 && arg1!=22050 && arg1!=11025 && arg1!=8000 ) {
                        fprintf( stderr, "Unsupported sample rate: %i.\n", arg1);
                        fprintf( stderr, "Supported sample rates are: 48000, 44100, 22050, 11025 and 8000.\n");
                        exit(3);
                    }
                    waveHeader.nSamplesPerSec = arg1;
                    waveHeader.nAvgBytesPerSec = arg1;
                }
                break;
            default:
                break;
        }
    }

    if ( optind < argc ) {
        if ( !(vz = fopen( argv[ optind ], "rb")) ) {
            fprintf( stderr, "Error opening %s.\n", argv[ optind ] );
            exit(4);
        }
        if ( optind < argc-1 ) {
            optind++;
        } else {
            argv[ optind ] = changeExtensionTo( defaultExtension, argv[ optind ] );
        }
        if ( !(output = fopen( argv[ optind ], "wb")) ) {
            fprintf( stderr, "Error opening %s.\n", argv[ optind ] );
            exit(4);
        }
        if ( verbose ) printf( "Create %s file\n", argv[ optind ] );
        if ( optind >= argc ) {
            fprintf( stderr, "Too many parameters: %s\n", argv[ optind+1 ] );
            exit(4);
        }
        if ( !cassetteOnly ) init_wav( output );
        process_vz( vz, output );
        if ( !cassetteOnly ) close_wav( output ); else fclose( output );
    } else {
        fprintf( stderr, "Source vz file not defined!\n\n" );
        print_usage();
        exit(4);
    }

    return 0;
}
