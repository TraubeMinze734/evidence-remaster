/** @file  exif.c
 *  @brief meta-data provider plug-in "EXIF"
 *
 *  get EXIF tags (JPEGs taken by digital cameras)
 *  code adapated from JHead by Matthias Wandel
 */

#define MY_NAME "EXIF"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "errors.h"
#include "meta.h"
#include "util.h"


#include <math.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>


#include <utime.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>



typedef unsigned char uchar;

#define PSEUDO_IMAGE_MARKER 0x123;


#define MAX_SECTIONS 20

#define MAX_COMMENT 2000
#define MAX_DATE_COPIES 10


typedef enum {
  FMT_BYTE=1,
  FMT_STRING,
  FMT_USHORT,
  FMT_ULONG,
  FMT_URATIONAL,
  FMT_SBYTE,
  FMT_UNDEFINED,
  FMT_SSHORT,
  FMT_SLONG,
  FMT_SRATIONAL,
  FMT_SINGLE,
  FMT_DOUBLE
} exif_format;

#define NUM_FORMATS FMT_DOUBLE

/*
 * JPEG markers consist of one or more 0xFF bytes, followed by a marker
 * code byte (which is not an FF).  Here are the marker codes of interest
 * in this program.  (See jdmarker.c for a more complete list.)
 */


#define M_SOF0  0xC0            /* Start Of Frame N */
#define M_SOF1  0xC1            /* N indicates which compression process */
#define M_SOF2  0xC2            /* Only SOF0-SOF2 are now in common use */
#define M_SOF3  0xC3
#define M_SOF5  0xC5            /* NB: codes C4 and CC are NOT SOF markers */
#define M_SOF6  0xC6
#define M_SOF7  0xC7
#define M_SOF9  0xC9
#define M_SOF10 0xCA
#define M_SOF11 0xCB
#define M_SOF13 0xCD
#define M_SOF14 0xCE
#define M_SOF15 0xCF
#define M_SOI   0xD8            /* Start Of Image (beginning of datastream) */
#define M_EOI   0xD9            /* End Of Image (end of datastream) */
#define M_SOS   0xDA            /* Start Of Scan (begins compressed data) */
#define M_JFIF  0xE0            /* JFIF marker */
#define M_EXIF  0xE1            /* EXIF marker */
#define M_COM   0xFE            /* COMment */



/* Describes tag values */

#define TAG_EXIF_OFFSET       0x8769
#define TAG_INTEROP_OFFSET    0xa005

#define TAG_MAKE              0x010F
#define TAG_MODEL             0x0110

#define TAG_ORIENTATION       0x0112

#define TAG_EXPOSURETIME      0x829A
#define TAG_FNUMBER           0x829D

#define TAG_SHUTTERSPEED      0x9201
#define TAG_APERTURE          0x9202
#define TAG_MAXAPERTURE       0x9205
#define TAG_FOCALLENGTH       0x920A

#define TAG_DATETIME           0x132
#define TAG_DATETIME_ORIGINAL  0x9003
#define TAG_DATETIME_DIGITIZED 0x9004
#define TAG_USERCOMMENT        0x9286

#define TAG_SUBJECT_DISTANCE  0x9206
#define TAG_FLASH             0x9209

#define TAG_FOCALPLANEXRES    0xa20E
#define TAG_FOCALPLANEUNITS   0xa210
#define TAG_EXIF_IMAGEWIDTH   0xA002
#define TAG_EXIF_IMAGELENGTH  0xA003

#define TAG_EXPOSURE_BIAS     0x9204
#define TAG_WHITEBALANCE      0xa403
#define TAG_METERING_MODE     0x9207
#define TAG_EXPOSURE_PROGRAM  0x8822
#define TAG_ISO_EQUIVALENT    0x8827

#define TAG_THUMBNAIL_OFFSET  0x0201
#define TAG_THUMBNAIL_LENGTH  0x0202

#define TAG_FOCALLENGTH_35MM  0xa405

/* Added by Quercus 17-1-2004 */
#define TAG_EXPOSURE_INDEX    0xa215
#define TAG_LIGHT_SOURCE      0x9208

#define TAG_MAKER_NOTE        0x927C



/*****************************************************************************/



typedef struct {
  uchar         *Data;
  int            Type;
  unsigned       Size;
} Section_t;



typedef struct {
  noir_md       *md;
  int            SectionsRead;
  char           CameraMake[32];
  char           CameraModel[40];
  char           DateTime[20];
  int            Height,Width;
  int            Orientation;
  int            IsColor;
  int            Process;
  int            FlashUsed;
  float          FocalLength;
  float          ExposureTime;
  float          ApertureFNumber;
  float          Distance;
  float          CCDWidth;
  float          ExposureBias;
  int            FocalLength35mmEquiv; /* Exif 2.2 tag - usually not present. */
  int            Whitebalance;
  int            MeteringMode;
  int            ExposureProgram;
  int            ISOequivalent;
  int            LightSource;
  char           Comments[MAX_COMMENT];

  unsigned char *ThumbnailPointer;  /* Pointer at the thumbnail */
  unsigned       ThumbnailSize;     /* Size of thumbnail. */
  char          *DateTimePointers[MAX_DATE_COPIES];
  int            numDateTimeTags;
} ImageInfo_t;



typedef struct {
  unsigned short      Tag;
  char               *Desc;
} TagTable_t;



/* FIXME ### */
static unsigned char *LastExifRefd;
static unsigned char *DirWithThumbnailPtrs;
static double         FocalplaneXRes;
static double         FocalplaneUnits;
static int            ExifImageWidth;
static int            MotorolaOrder=0;

static void          *OrientationPtr;
static int            OrientationNumFormat; 



/* Table of JPEG encoding process names */
static TagTable_t ProcessTable[] = {
  { M_SOF0,   "Baseline"},
  { M_SOF1,   "Extended sequential"},
  { M_SOF2,   "Progressive"},
  { M_SOF3,   "Lossless"},
  { M_SOF5,   "Differential sequential"},
  { M_SOF6,   "Differential progressive"},
  { M_SOF7,   "Differential lossless"},
  { M_SOF9,   "Extended sequential, arithmetic coding"},
  { M_SOF10,  "Progressive, arithmetic coding"},
  { M_SOF11,  "Lossless, arithmetic coding"},
  { M_SOF13,  "Differential sequential, arithmetic coding"},
  { M_SOF14,  "Differential progressive, arithmetic coding"},
  { M_SOF15,  "Differential lossless, arithmetic coding"},
  { 0,        "Unknown"}
};



/*
 1 - "The 0th row is at the visual top of the image,    and the 0th column is the visual left-hand side."
 2 - "The 0th row is at the visual top of the image,    and the 0th column is the visual right-hand side."
 3 - "The 0th row is at the visual bottom of the image, and the 0th column is the visual right-hand side."
 4 - "The 0th row is at the visual bottom of the image, and the 0th column is the visual left-hand side."

 5 - "The 0th row is the visual left-hand side of of the image,  and the 0th column is the visual top."
 6 - "The 0th row is the visual right-hand side of of the image, and the 0th column is the visual top."
 7 - "The 0th row is the visual right-hand side of of the image, and the 0th column is the visual bottom."
 8 - "The 0th row is the visual left-hand side of of the image,  and the 0th column is the visual bottom."

 Note: The descriptions here are the same as the name of the command line
 option to pass to jpegtran to right the image
*/



static const char * OrientTab[9] = {
  "Undefined",
  "Normal",           /* 1 */
  "flip horizontal",  /* left right reversed mirror */
  "rotate 180",       /* 3 */
  "flip vertical",    /* upside down mirror */
  "transpose",        /* Flipped about top-left <--> bottom-right axis. */
  "rotate 90",        /* rotate 90 cw to right it. */
  "transverse",       /* flipped about top-right <--> bottom-left axis */
  "rotate 270",       /* rotate 270 to right it. */
};



int BytesPerFormat[] = {0,1,1,2,4,8,1,1,2,4,8,4,8};



static TagTable_t TagTable[] = {
  { 0x001,   "InteropIndex"},
  { 0x002,   "InteropVersion"},
  { 0x100,   "ImageWidth"},
  { 0x101,   "ImageLength"},
  { 0x102,   "BitsPerSample"},
  { 0x103,   "Compression"},
  { 0x106,   "PhotometricInterpretation"},
  { 0x10A,   "FillOrder"},
  { 0x10D,   "DocumentName"},
  { 0x10E,   "ImageDescription"},
  { 0x10F,   "Make"},
  { 0x110,   "Model"},
  { 0x111,   "StripOffsets"},
  { 0x112,   "Orientation"},
  { 0x115,   "SamplesPerPixel"},
  { 0x116,   "RowsPerStrip"},
  { 0x117,   "StripByteCounts"},
  { 0x11A,   "XResolution"},
  { 0x11B,   "YResolution"},
  { 0x11C,   "PlanarConfiguration"},
  { 0x128,   "ResolutionUnit"},
  { 0x12D,   "TransferFunction"},
  { 0x131,   "Software"},
  { 0x132,   "DateTime"},
  { 0x13B,   "Artist"},
  { 0x13E,   "WhitePoint"},
  { 0x13F,   "PrimaryChromaticities"},
  { 0x156,   "TransferRange"},
  { 0x200,   "JPEGProc"},
  { 0x201,   "ThumbnailOffset"},
  { 0x202,   "ThumbnailLength"},
  { 0x211,   "YCbCrCoefficients"},
  { 0x212,   "YCbCrSubSampling"},
  { 0x213,   "YCbCrPositioning"},
  { 0x214,   "ReferenceBlackWhite"},
  { 0x1001,  "RelatedImageWidth"},
  { 0x1002,  "RelatedImageLength"},
  { 0x828D,  "CFARepeatPatternDim"},
  { 0x828E,  "CFAPattern"},
  { 0x828F,  "BatteryLevel"},
  { 0x8298,  "Copyright"},
  { 0x829A,  "ExposureTime"},
  { 0x829D,  "FNumber"},
  { 0x83BB,  "IPTC/NAA"},
  { 0x8769,  "ExifOffset"},
  { 0x8773,  "InterColorProfile"},
  { 0x8822,  "ExposureProgram"},
  { 0x8824,  "SpectralSensitivity"},
  { 0x8825,  "GPSInfo"},
  { 0x8827,  "ISOSpeedRatings"},
  { 0x8828,  "OECF"},
  { 0x9000,  "ExifVersion"},
  { 0x9003,  "DateTimeOriginal"},
  { 0x9004,  "DateTimeDigitized"},
  { 0x9101,  "ComponentsConfiguration"},
  { 0x9102,  "CompressedBitsPerPixel"},
  { 0x9201,  "ShutterSpeedValue"},
  { 0x9202,  "ApertureValue"},
  { 0x9203,  "BrightnessValue"},
  { 0x9204,  "ExposureBiasValue"},
  { 0x9205,  "MaxApertureValue"},
  { 0x9206,  "SubjectDistance"},
  { 0x9207,  "MeteringMode"},
  { 0x9208,  "LightSource"},
  { 0x9209,  "Flash"},
  { 0x920A,  "FocalLength"},
  { 0x927C,  "MakerNote"},
  { 0x9286,  "UserComment"},
  { 0x9290,  "SubSecTime"},
  { 0x9291,  "SubSecTimeOriginal"},
  { 0x9292,  "SubSecTimeDigitized"},
  { 0xA000,  "FlashPixVersion"},
  { 0xA001,  "ColorSpace"},
  { 0xA002,  "ExifImageWidth"},
  { 0xA003,  "ExifImageLength"},
  { 0xA004,  "RelatedAudioFile"},
  { 0xA005,  "InteroperabilityOffset"},
  { 0xA20B,  "FlashEnergy"},                 /* 0x920B in TIFF/EP */
  { 0xA20C,  "SpatialFrequencyResponse"},  /* 0x920C    -  - */
  { 0xA20E,  "FocalPlaneXResolution"},     /* 0x920E    -  - */
  { 0xA20F,  "FocalPlaneYResolution"},      /* 0x920F    -  - */
  { 0xA210,  "FocalPlaneResolutionUnit"},  /* 0x9210    -  - */
  { 0xA214,  "SubjectLocation"},             /* 0x9214    -  - */
  { 0xA215,  "ExposureIndex"},            /* 0x9215    -  - */
  { 0xA217,  "SensingMethod"},            /* 0x9217    -  - */
  { 0xA300,  "FileSource"},
  { 0xA301,  "SceneType"},
  { 0xA301,  "CFA Pattern"},
  { 0xa401,  "CustomRendered"},
  { 0xa402,  "ExposureMode"},
  { 0xa403,  "WhiteBalance"},
  { 0xa404,  "DigitalZoomRatio"},
  { 0xa405,  "FocalLengthIn35mmFilm"},
  { 0xa406,  "SceneCaptureType"},
  { 0xa407,  "GainControl"},
  { 0xa408,  "Contrast"},
  { 0xa409,  "Saturation"},
  { 0xa40a,  "Sharpness"},
  { 0xa40c,  "SubjectDistanceRange"}
} ;

const int SizeTagTable = sizeof( TagTable ) / sizeof( TagTable_t );



/*****************************************************************************/



/* Convert a 16 bit unsigned value from file's native byte order */
static inline int Get16u(void *Short) {
  if(MotorolaOrder)
    return (((uchar *)Short)[0] << 8) | ((uchar *)Short)[1];
  else
    return (((uchar *)Short)[1] << 8) | ((uchar *)Short)[0]; }



/* Convert a 32 bit signed value from file's native byte order */
static inline int Get32s(void * Long) {
  if(MotorolaOrder)
    return  ((( char *)Long)[0] << 24) | (((uchar *)Long)[1] << 16)
      | (((uchar *)Long)[2] << 8 ) | (((uchar *)Long)[3] << 0 );
  else
    return  ((( char *)Long)[3] << 24) | (((uchar *)Long)[2] << 16)
      | (((uchar *)Long)[1] << 8 ) | (((uchar *)Long)[0] << 0 ); }



/* Convert a 32 bit unsigned value from file's native byte order */
static unsigned Get32u(void * Long) {
  return (unsigned)Get32s(Long) & 0xffffffff; }



/* Display a number as one of its many formats */
static void PrintFormatNumber(void *ValuePtr,int Format,int ByteCount) {
  switch(Format){
    case FMT_SBYTE:
    case FMT_BYTE:      E(2,"%02x\n",*(uchar *)ValuePtr);            break;
    case FMT_USHORT:    E(2,"%d\n",Get16u(ValuePtr));                break;
    case FMT_ULONG:     
    case FMT_SLONG:     E(2,"%d\n",Get32s(ValuePtr));                break;
    case FMT_SSHORT:    E(2,"%hd\n",(signed short)Get16u(ValuePtr)); break;
    case FMT_URATIONAL:
    case FMT_SRATIONAL: 
      E(2,"%d/%d\n",Get32s(ValuePtr), Get32s(4+(char *)ValuePtr)); break;

    case FMT_SINGLE:    E(2,"%f\n",(double)*(float *)ValuePtr);   break;
    case FMT_DOUBLE:    E(2,"%f\n",*(double *)ValuePtr);          break;
    default: 
      E(0,"Unknown format %d:", Format); }}



/* Evaluate number, be it int, rational, or float from directory. */
static  double ConvertAnyFormat(void *ValuePtr,int Format) {
  double Value;
  Value = 0;

  switch(Format){
    case FMT_SBYTE:     Value = *(signed char *)ValuePtr;  break;
    case FMT_BYTE:      Value = *(uchar *)ValuePtr;        break;

    case FMT_USHORT:    Value = Get16u(ValuePtr);          break;
    case FMT_ULONG:     Value = Get32u(ValuePtr);          break;

    case FMT_URATIONAL:
    case FMT_SRATIONAL: 
    {
      int Num,Den;
      Num = Get32s(ValuePtr);
      Den = Get32s(4+(char *)ValuePtr);
      if (Den == 0){
        Value = 0;
      }else{
        Value = (double)Num/Den;
      }
      break;
    }

    case FMT_SSHORT:    Value = (signed short)Get16u(ValuePtr);  break;
    case FMT_SLONG:     Value = Get32s(ValuePtr);                break;

      /* Not sure if this is correct (never seen float used in Exif format) */
    case FMT_SINGLE:    Value = (double)*(float *)ValuePtr;      break;
    case FMT_DOUBLE:    Value = *(double *)ValuePtr;             break;
  }
  return Value;
}



/* Process one of the nested EXIF directories. */
static void ProcessExifDir(ImageInfo_t *ImageInfo,unsigned char *DirStart,unsigned char *OffsetBase,
                           unsigned ExifLength,int NestingLevel)
{
  int de;
  int a;
  int NumDirEntries;
  unsigned ThumbnailOffset = 0;
  unsigned ThumbnailSize = 0;
  char IndentString[25];

  if(NestingLevel>4) {
    E(0,"Maximum directory nesting exceeded (corrupt EXIF header?)\n");
    return; }

  memset(IndentString, ' ', 25);
  IndentString[NestingLevel * 4] = '\0';

  NumDirEntries = Get16u(DirStart);
#define DIR_ENTRY_ADDR(Start, Entry) (Start+2+12*(Entry))

  {
    unsigned char * DirEnd;
    DirEnd = DIR_ENTRY_ADDR(DirStart, NumDirEntries);
    if (DirEnd+4 > (OffsetBase+ExifLength)){
      if (DirEnd+2 == OffsetBase+ExifLength || DirEnd == OffsetBase+ExifLength){
        /* Version 1.3 of jhead would truncate a bit too much.
           This also caught later on as well. */
      }else{
        /* Note: Files that had thumbnails trimmed with jhead 1.3 or earlier
           might trigger this. */
        E(0,"Illegally sized directory\n");
        return;
      }
    }
    if (DirEnd > LastExifRefd) LastExifRefd = DirEnd;
  }

  E(1,"(dir has %d entries)\n",NumDirEntries);

  for (de=0;de<NumDirEntries;de++){
    int Tag, Format, Components;
    unsigned char * ValuePtr;
    int ByteCount;
    char * DirEntry;
    DirEntry = DIR_ENTRY_ADDR(DirStart, de);

    Tag = Get16u(DirEntry);
    Format = Get16u(DirEntry+2);
    Components = Get32u(DirEntry+4);

    if ((Format-1) >= NUM_FORMATS) {
      /* (-1) catches illegal zero case as unsigned underflows to positive large. */
      E(0,"Illegal number format %d for tag %04x\n", Format, Tag);
      continue;
    }

    ByteCount = Components * BytesPerFormat[Format];

    if (ByteCount > 4){
      unsigned OffsetVal;
      OffsetVal = Get32u(DirEntry+8);
      /* If its bigger than 4 bytes, the dir entry contains an offset. */
      if (OffsetVal+ByteCount > ExifLength){
        /* Bogus pointer offset and / or bytecount value */
        E(0,"Illegal value pointer for tag %04x\n",Tag);
        continue; }
      ValuePtr = OffsetBase+OffsetVal; }

    else {
      /* 4 bytes or less and value is in the dir entry itself */
      ValuePtr = DirEntry+8; }

    if(LastExifRefd<ValuePtr+ByteCount) {
      /* Keep track of last byte in the exif header that was actually referenced.
         That way, we know where the discardable thumbnail data begins. */
      LastExifRefd = ValuePtr+ByteCount; }

    if(Tag==TAG_MAKER_NOTE) {
      E(1,"%s    Maker note: \n",IndentString);
      /* ProcessMakerNote(ValuePtr, ByteCount, OffsetBase, ExifLength); */
      continue; }

    /* Show tag name */
    for(a=0;;a++) {
      if(a>=SizeTagTable ) {
        E(2,"%s    Unknown Tag %04x Value = ",IndentString,Tag);
        break; }
      if(TagTable[a].Tag==Tag) {
        E(2,"%s    %s = ",IndentString,TagTable[a].Desc);
        break; }}

    /* Show tag value. */
    switch(Format){
      case FMT_UNDEFINED:
        /* Undefined is typically an ASCII string. */

        case FMT_STRING:
          /* String arrays printed without function call (different from int arrays) */
        { int NoPrint=0;
          char *m=malloc(ByteCount+1),*p=m;
          if(m) {
            for(a=0;a<ByteCount;a++) {
              if(ValuePtr[a]>=' ') {
                *(p++)=ValuePtr[a];
                NoPrint=0; }
              else{
                /* Avoiding indicating too many unprintable characters of proprietary
                   bits of binary information this program may not know how to parse. */
                if(!NoPrint){
                  *(p++)='?';
                  NoPrint=1;
                }
              }
            }
            E(2,"\"%s\"\n",m);
            free(m);
          }
        }
        break;

        default:
          /* Handle arrays of numbers later (will there ever be?) */
          PrintFormatNumber(ValuePtr, Format, ByteCount);
    }

    /* Extract useful components of tag */
    switch(Tag){

      case TAG_MAKE:
        strncpy(ImageInfo->CameraMake, ValuePtr, 31);
        break;

      case TAG_MODEL:
        strncpy(ImageInfo->CameraModel, ValuePtr, 39);
        break;


      case TAG_DATETIME_ORIGINAL:
        /* If we get a DATETIME_ORIGINAL, we use that one. */
        strncpy(ImageInfo->DateTime, ValuePtr, 19);
        /* Fallthru... */

      case TAG_DATETIME_DIGITIZED:
      case TAG_DATETIME:
        if (!isdigit(ImageInfo->DateTime[0])){
          /* If we don't already have a DATETIME_ORIGINAL, use whatever
             time fields we may have. */
          strncpy(ImageInfo->DateTime, ValuePtr, 19);
        }

        if (ImageInfo->numDateTimeTags >= MAX_DATE_COPIES){
          E(0,"More than %d date fields!",MAX_DATE_COPIES);
          break;
        }
        ImageInfo->DateTimePointers[ImageInfo->numDateTimeTags++] = ValuePtr;
        break;


      case TAG_USERCOMMENT:
        /* Olympus has this padded with trailing spaces.  Remove these first. */
        for (a=ByteCount;;){
          a--;
          if ((ValuePtr)[a] == ' '){
            (ValuePtr)[a] = '\0';
          }else{
            break;
          }
          if (a == 0) break;
        }

        /* Copy the comment */
        if (memcmp(ValuePtr, "ASCII",5) == 0){
          for (a=5;a<10;a++){
            int c;
            c = (ValuePtr)[a];
            if (c != '\0' && c != ' '){
              strncpy(ImageInfo->Comments, a+ValuePtr, 199);
              break;
            }
          }
                    
        }else{
          strncpy(ImageInfo->Comments, ValuePtr, 199);
        }
        break;

      case TAG_FNUMBER:
        /* Simplest way of expressing aperture, so I trust it the most.
           (overwrite previously computd value if there is one) */
        ImageInfo->ApertureFNumber = (float)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_APERTURE:
      case TAG_MAXAPERTURE:
        /* More relevant info always comes earlier, so only use this field if we don't 
           have appropriate aperture information yet. */
        if (ImageInfo->ApertureFNumber == 0){
          ImageInfo->ApertureFNumber 
            = (float)exp(ConvertAnyFormat(ValuePtr, Format)*log(2)*0.5);
        }
        break;

      case TAG_FOCALLENGTH:
        /* Nice digital cameras actually save the focal length as a function
           of how far they are zoomed in. */
        ImageInfo->FocalLength = (float)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_SUBJECT_DISTANCE:
        /* Inidcates the distacne the autofocus camera is focused to.
           Tends to be less accurate as distance increases. */
        ImageInfo->Distance = (float)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_EXPOSURETIME:
        /* Simplest way of expressing exposure time, so I trust it most.
           (overwrite previously computd value if there is one) */
        ImageInfo->ExposureTime = (float)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_SHUTTERSPEED:
        /* More complicated way of expressing exposure time, so only use
           this value if we don't already have it from somewhere else. */
        if (ImageInfo->ExposureTime == 0){
          ImageInfo->ExposureTime 
            = (float)(1/exp(ConvertAnyFormat(ValuePtr, Format)*log(2)));
        }
        break;

      case TAG_FLASH:
        if ((int)ConvertAnyFormat(ValuePtr, Format) & 1){
          ImageInfo->FlashUsed = TRUE;
        }else{
          ImageInfo->FlashUsed = FALSE;
        }
        break;

      case TAG_ORIENTATION:
        ImageInfo->Orientation = (int)ConvertAnyFormat(ValuePtr, Format);
        OrientationPtr = ValuePtr;
        OrientationNumFormat = Format;
        if (ImageInfo->Orientation < 1 || ImageInfo->Orientation > 8){
          E(0,"Undefined rotation value %d\n", ImageInfo->Orientation);
          ImageInfo->Orientation = 0;
        }
        break;

      case TAG_EXIF_IMAGELENGTH:
      case TAG_EXIF_IMAGEWIDTH:
        /* Use largest of height and width to deal with images that have been
           rotated to portrait format. */
        a = (int)ConvertAnyFormat(ValuePtr, Format);
        if (ExifImageWidth < a) ExifImageWidth = a;
        break;

      case TAG_FOCALPLANEXRES:
        FocalplaneXRes = ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_FOCALPLANEUNITS:
        switch((int)ConvertAnyFormat(ValuePtr, Format)){
          case 1: FocalplaneUnits = 25.4; break; /* inch */
          case 2: 
            /* According to the information I was using, 2 means meters.
               But looking at the Cannon powershot's files, inches is the only
               sensible value. */
            FocalplaneUnits = 25.4;
            break;

          case 3: FocalplaneUnits = 10;   break;  /* centimeter */
          case 4: FocalplaneUnits = 1;    break;  /* milimeter */
          case 5: FocalplaneUnits = .001; break;  /* micrometer */
        }
        break;

        /* Remaining cases contributed by: Volker C. Schoech (schoech@gmx.de) */

      case TAG_EXPOSURE_BIAS:
        ImageInfo->ExposureBias = (float)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_WHITEBALANCE:
        ImageInfo->Whitebalance = (int)ConvertAnyFormat(ValuePtr, Format);
        break;

      /* Quercus: 17-1-2004 Lightsource */
      case TAG_LIGHT_SOURCE:
        ImageInfo->LightSource = (int)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_METERING_MODE:
        ImageInfo->MeteringMode = (int)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_EXPOSURE_PROGRAM:
        ImageInfo->ExposureProgram = (int)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_EXPOSURE_INDEX:
        if (ImageInfo->ISOequivalent == 0){
          /* Exposure index and ISO equivalent are often used interchangeably,
             so we will do the same in jhead.
             http://photography.about.com/library/glossary/bldef_ei.htm */
          ImageInfo->ISOequivalent = (int)ConvertAnyFormat(ValuePtr, Format);
        }
        break;

      case TAG_ISO_EQUIVALENT:
        ImageInfo->ISOequivalent = (int)ConvertAnyFormat(ValuePtr, Format);
        if ( ImageInfo->ISOequivalent < 50 ){
          /* Fixes strange encoding on some older digicams. */
          ImageInfo->ISOequivalent *= 200;
        }
        break;

      case TAG_THUMBNAIL_OFFSET:
        ThumbnailOffset = (unsigned)ConvertAnyFormat(ValuePtr, Format);
        DirWithThumbnailPtrs = DirStart;
        break;

      case TAG_THUMBNAIL_LENGTH:
        ThumbnailSize = (unsigned)ConvertAnyFormat(ValuePtr, Format);
        break;

      case TAG_EXIF_OFFSET:
        E(1,"%s    EXIF Dir:",IndentString);

      case TAG_INTEROP_OFFSET:
        if (Tag == TAG_INTEROP_OFFSET)
          E(2,"%s    Interop Dir:",IndentString);
        {
          unsigned char * SubdirStart;
          SubdirStart = OffsetBase + Get32u(ValuePtr);
          if (SubdirStart < OffsetBase || SubdirStart > OffsetBase+ExifLength){
            E(0,"Illegal exif or interop ofset directory link\n");
          } else
            ProcessExifDir(ImageInfo,SubdirStart,OffsetBase,ExifLength,NestingLevel+1);
          continue;
        }

      case TAG_FOCALLENGTH_35MM:
        /* The focal length equivalent 35 mm is a 2.2 tag (defined as of April 2002)
           if its present, use it to compute equivalent focal length instead of 
           computing it from sensor geometry and actual focal length. */
        ImageInfo->FocalLength35mmEquiv = (unsigned)ConvertAnyFormat(ValuePtr, Format);
        break;
    }
  }


  {
    /* In addition to linking to subdirectories via exif tags, 
       there's also a potential link to another directory at the end of each
       directory.  this has got to be the result of a comitee! */
    unsigned char * SubdirStart;
    unsigned Offset;

    if (DIR_ENTRY_ADDR(DirStart, NumDirEntries) + 4 <= OffsetBase+ExifLength){
      Offset = Get32u(DirStart+2+12*NumDirEntries);
      if (Offset){
        SubdirStart = OffsetBase + Offset;
        if (SubdirStart > OffsetBase+ExifLength){
          if (SubdirStart < OffsetBase+ExifLength+20){
            /* Jhead 1.3 or earlier would crop the whole directory!
               As Jhead produces this form of format incorrectness, 
               I'll just let it pass silently */
            E(0,"Thumbnail removed with Jhead 1.3 or earlier\n");
          }else{
            E(0,"Illegal subdirectory link\n");
          }
        }else{
          if (SubdirStart <= OffsetBase+ExifLength){
            E(2,"%s    Continued directory ",IndentString);
            ProcessExifDir(ImageInfo,SubdirStart, OffsetBase, ExifLength, NestingLevel+1);
          }
        }
      }
    }else{
      /* The exif header ends before the last next directory pointer. */
    }
  }


  if (ThumbnailSize && ThumbnailOffset){
    if (ThumbnailSize + ThumbnailOffset <= ExifLength){
      /* The thumbnail pointer appears to be valid.  Store it. */
      ImageInfo->ThumbnailPointer = OffsetBase + ThumbnailOffset;
      ImageInfo->ThumbnailSize = ThumbnailSize;

      E(2,"Thumbnail size: %d bytes\n",ThumbnailSize);
    }
  }
}



/* Process a COM marker.
   We want to print out the marker contents as legible text;
   we must guard against random junk and varying newline representations. */
static void process_COM(ImageInfo_t *ImageInfo,const uchar *Data,int length) {
  int ch;
  char Comment[MAX_COMMENT+1];
  int nch;
  int a;

  nch = 0;

  if (length > MAX_COMMENT) length = MAX_COMMENT; /* Truncate if it won't fit in our structure. */

  for (a=2;a<length;a++){
    ch = Data[a];

    if (ch == '\r' && Data[a+1] == '\n') continue; /* Remove cr followed by lf. */

    if (ch >= 32 || ch == '\n' || ch == '\t'){
      Comment[nch++] = (char)ch;
    }else{
      Comment[nch++] = '?';
    }
  }

  Comment[nch] = '\0';

  E(4,"COM marker comment: %s\n",Comment);

  strcpy(ImageInfo->Comments,Comment); }

 

/* Get 16 bits motorola order (always) for jpeg header stuff. */
static int Get16m(const void *Short) {
  return (((uchar *)Short)[0] << 8) | ((uchar *)Short)[1]; }



/* Process a SOFn marker.  This is useful for the image dimensions */
static void process_SOFn(ImageInfo_t *ImageInfo,const uchar *Data,int marker) {
  int data_precision, num_components;

  data_precision = Data[2];
  ImageInfo->Height = Get16m(Data+3);
  ImageInfo->Width = Get16m(Data+5);
  num_components = Data[7];

  if (num_components == 3){
    ImageInfo->IsColor = 1;
  }else{
    ImageInfo->IsColor = 0;
  }

  ImageInfo->Process = marker;

  E(1,"JPEG image is %u * %u, %d color components, %d bits per sample\n",
    ImageInfo->Width, ImageInfo->Height, num_components, data_precision); }



/**
 * Process a EXIF marker -- describes all the drivel that most digital cameras include...
 */
static int process_EXIF(ImageInfo_t *ImageInfo,unsigned char *ExifSection,unsigned int length) {
  int FirstOffset;

  FocalplaneXRes = 0;
  FocalplaneUnits = 0;
  ExifImageWidth = 0;
  OrientationPtr = NULL;


  E(1,"EXIF header: %d bytes\n",length);

  { /* Check the EXIF header component */
    static uchar ExifHeader[] = "Exif\0\0";
    if (memcmp(ExifSection+2, ExifHeader,6)){
      E(0,"Incorrect EXIF header\n");
      return NOIR_FAIL; }}

  if(memcmp(ExifSection+8,"II",2)==0) {
    E(2,"EXIF section in Intel order\n");
    MotorolaOrder=0; }
  else {
    if(memcmp(ExifSection+8,"MM",2)==0){
      E(2,"EXIF section in Motorola order\n");
      MotorolaOrder=1; }
    else{
      E(0,"Invalid Exif alignment marker.\n");
      return NOIR_NOTSUPP; }}

  /* Check the next value for correctness. */
  if(Get16u(ExifSection+10)!=0x2a) {
    E(0,"Invalid EXIF start (1)\n");
    return NOIR_FAIL; }

  FirstOffset = Get32u(ExifSection+12);
  if(FirstOffset<8||FirstOffset>16) {
    /* I used to ensure this was set to 8 (website I used indicated its 8)
       but PENTAX Optio 230 has it set differently, and uses it as offset. (Sept 11 2002) */
    E(0,"Suspicious offset of first IFD value\n"); }

  LastExifRefd=ExifSection;
  DirWithThumbnailPtrs=NULL;

  /* First directory starts 16 bytes in.  All offset are relative to 8 bytes in. */
  ProcessExifDir(ImageInfo,ExifSection+8+FirstOffset, ExifSection+8, length-6, 0);

  /* Compute the CCD width, in milimeters. */
  if(FocalplaneXRes!=0){
    /* Note: With some cameras, its not possible to compute this correctly because
       they don't adjust the indicated focal plane resolution units when using less
       than maximum resolution, so the CCDWidth value comes out too small.  Nothing
       that Jhad can do about it - its a camera problem. */
    ImageInfo->CCDWidth = (float)(ExifImageWidth * FocalplaneUnits / FocalplaneXRes);

    if (ImageInfo->FocalLength && ImageInfo->FocalLength35mmEquiv == 0){
      /* Compute 35 mm equivalent focal length based on sensor geometry if we haven't
         already got it explicitly from a tag. */
      ImageInfo->FocalLength35mmEquiv = (int)(ImageInfo->FocalLength/ImageInfo->CCDWidth*36 + 0.5);
    }
  }

  E(1,"Non settings part of EXIF header: %d bytes\n",ExifSection+length-LastExifRefd);
  return NOIR_SUCC; }



/*****************************************************************************/



/**
 * Parse the marker stream until SOS or EOI is seen
 */
static int ReadJpegSections(ImageInfo_t *ImageInfo,Section_t Sections[],FILE *infile) {
  int a;
  int HaveCom = FALSE;

  a=fgetc(infile);

  if (a != 0xff || fgetc(infile) != M_SOI){
    return NOIR_TYPEMISMATCH;
  }
  for(;;){
    int itemlen;
    int marker = 0;
    int ll,lh, got;
    uchar * Data;

    if (ImageInfo->SectionsRead >= MAX_SECTIONS){
      E(0,"Too many sections in JPEG file");
      return NOIR_OOM;
    }

    for (a=0;a<7;a++){
      marker = fgetc(infile);
      if (marker != 0xff) break;

      if (a >= 6){
        E(0,"too many padding bytes\n");
        return NOIR_TYPEMISMATCH;
      }
    }

    if (marker == 0xff){
      /* 0xff is legal padding, but if we get that many, something's wrong. */
      puts("too many padding bytes!");
      return NOIR_TYPEMISMATCH;
    }

    Sections[ImageInfo->SectionsRead].Type = marker;
  
    /* Read the length of the section. */
    lh = fgetc(infile);
    ll = fgetc(infile);

    itemlen = (lh << 8) | ll;

    if (itemlen < 2){
      puts("invalid marker");
      return NOIR_TYPEMISMATCH;
    }

    Sections[ImageInfo->SectionsRead].Size = itemlen;

    Data = (uchar *)malloc(itemlen);
    if (Data == NULL){
      puts("Could not allocate memory");
      return NOIR_OOM;
    }
    Sections[ImageInfo->SectionsRead].Data = Data;

    /* Store first two pre-read bytes. */
    Data[0] = (uchar)lh;
    Data[1] = (uchar)ll;

    got = fread(Data+2, 1, itemlen-2, infile); /* Read the whole section. */
    if (got != itemlen-2){
      puts("Premature end of file?");
      return NOIR_NODATA;
    }
    ImageInfo->SectionsRead += 1;

    switch(marker){

      case M_SOS:   /* stop before hitting compressed data */
        return NOIR_SUCC;

      case M_EOI:   /* in case it's a tables-only JPEG stream */
        E(0,"No image in JPEG!\n");
        return NOIR_PARTIAL;

      case M_COM: /* Comment section */
        if (HaveCom){
          /* Discard this section. */
          free(Sections[--ImageInfo->SectionsRead].Data); }
        else {
          process_COM(ImageInfo, Data, itemlen);
          HaveCom = TRUE; }
        break;

      case M_JFIF:
        /* Regular JPEGs always have this tag, EXIF images have the EXIF
           marker instead, althogh ACDsee will write images with both markers.
           this program will re-create this marker on absence of EXIF marker.
           hence no need to keep the copy from the file. */
        free(Sections[--ImageInfo->SectionsRead].Data);
        break;

      case M_EXIF:
        /* Seen files from some 'U-lead' software with Vivitar scanner
           that uses marker 31 for non EXIF stuff.  Thus make sure 
           it says 'Exif' in the section before treating it as EXIF. */
        if(memcmp(Data+2, "Exif", 4) == 0)
          process_EXIF(ImageInfo,(char *)Data,itemlen);
        else{
          /* Discard this section. */
          free(Sections[--ImageInfo->SectionsRead].Data);
        }
        break;

      case M_SOF0: 
      case M_SOF1: 
      case M_SOF2: 
      case M_SOF3: 
      case M_SOF5: 
      case M_SOF6: 
      case M_SOF7: 
      case M_SOF9: 
      case M_SOF10:
      case M_SOF11:
      case M_SOF13:
      case M_SOF14:
      case M_SOF15:
        process_SOFn(ImageInfo, Data, marker);
        break;
      default:
        /* Skip any other sections. */
        E(1,"skipping JPEG section 0x%02x (%d bytes)\n",marker,itemlen);
        break;
    }
  }
  return NOIR_SUCC; }



/*****************************************************************************/



/**
 * Show the collected image info, displaying camera F-stop and shutter speed in a consistent and legible fashion.
 */
#define MA(k,d,v) meta_add(ImageInfo->md,META_RO,META_STR,(k),(d),MY_NAME,(v),prov)

static void ShowImageInfo(ImageInfo_t *ImageInfo,noir_provider *prov) {
  estring *e=estring_new(256);
  char    *k;

  if(ImageInfo->CameraMake[0]) {
    MA("Camera make","Manufacturer",ImageInfo->CameraMake);
    MA("Camera model","Model",ImageInfo->CameraModel); }

  if(ImageInfo->DateTime[0])
    MA("Date/Time","Date and time when the picture was taken",ImageInfo->DateTime);

  estring_printf(e,"%d x %d",ImageInfo->Width,ImageInfo->Height);
  MA("Resolution","Width x Height",ESTRING_GET_CSTRING(e));

  if(ImageInfo->Orientation>1) /* Only print orientation if one was supplied, and if its not 1 (normal orientation) */
    MA("Orientation","Hetero or homo",OrientTab[ImageInfo->Orientation]);

  MA("Palette","Colour or black & white?",(ImageInfo->IsColor==0)?"Black and white":"Colour");

  if(ImageInfo->FlashUsed>=0)
    MA("Flash","Was the flash used when taking the picture?",ImageInfo->FlashUsed?"Yes":"No");

  if(ImageInfo->FocalLength){
    estring_printf(e,"%4.1fmm",(double)ImageInfo->FocalLength);
    if(ImageInfo->FocalLength35mmEquiv)
      estring_appendf(e," (35mm equivalent: %dmm)",ImageInfo->FocalLength35mmEquiv);
    MA("Focal length",NULL,ESTRING_GET_CSTRING(e)); }

  if(ImageInfo->CCDWidth){
    estring_printf(e,"%4.2fmm",(double)ImageInfo->CCDWidth);
    MA("CCD width",NULL,ESTRING_GET_CSTRING(e)); }

  if(ImageInfo->ExposureTime) {
    if(ImageInfo->ExposureTime<0.010)
      estring_printf(e,"%6.4f s ",(double)ImageInfo->ExposureTime);
    else
      estring_printf(e,"%5.3f s ",(double)ImageInfo->ExposureTime);
    if(ImageInfo->ExposureTime<=0.5)
      estring_appendf(e," (1/%d)",(int)(0.5 + 1/ImageInfo->ExposureTime));
    MA("Exposure time",NULL,ESTRING_GET_CSTRING(e)); }

  if(ImageInfo->ApertureFNumber){
    estring_printf(e,"f/%3.1f",(double)ImageInfo->ApertureFNumber);
    MA("Aperture",NULL,ESTRING_GET_CSTRING(e)); }

  if(ImageInfo->Distance) {
    if(ImageInfo->Distance<0)
      estring_printf(e,"infinite");
    else
      estring_printf(e,"%4.2fm",(double)ImageInfo->Distance);
    MA("Focus dist.",NULL,ESTRING_GET_CSTRING(e)); }

  if(ImageInfo->ISOequivalent) {
    estring_printf(e,"%2d",(int)ImageInfo->ISOequivalent);
    MA("ISO equiv",NULL,ESTRING_GET_CSTRING(e)); }

  if(ImageInfo->ExposureBias) {
    /* If exposure bias was specified, but set to zero, presumably its no bias at all,
       so only show it if its nonzero. */
    estring_printf(e,"%4.2f",(double)ImageInfo->ExposureBias);
    MA("Expose bias",NULL,ESTRING_GET_CSTRING(e)); }
        
  if(ImageInfo->Whitebalance) {
    k="Whitebalance";
    switch(ImageInfo->Whitebalance) {
      case 1:
        MA(k,NULL,"manual");
        break;
      case 0:
        MA(k,NULL,"Auto");
        break;
      default:
        MA(k,NULL,"auto");
    }
  }



  /* Quercus: 17-1-2004 Added LightSource, some cams return this, whitebalance or both */
  k="Light Source";
  switch(ImageInfo->LightSource) {
    case 1:
      MA(k,NULL,"daylight");
      break;
    case 2:
      MA(k,NULL,"fluorescent");
      break;
    case 3:
      MA(k,NULL,"incandescent");
      break;
    case 4:
      MA(k,NULL,"flash");
      break;
    case 9:
      MA(k,NULL,"fine weather");
      break;
    case 11:
      MA(k,NULL,"shade");
      break;
    default:; /* Quercus: 17-1-2004 There are many more modes for this, check Exif2.2 specs
                 If it just says 'unknown' or we don't know it, then
                 don't bother showing it - it doesn't add any useful information. */
  }



  if (ImageInfo->MeteringMode){ /* 05-jan-2001 vcs */
    k="Metering Mode";
    switch(ImageInfo->MeteringMode) {
      case 2:
        MA(k,NULL,"centre weight");
        break;
      case 3:
        MA(k,NULL,"spot");
        break;
      case 5:
        MA(k,NULL,"matrix");
        break; }}



  if (ImageInfo->ExposureProgram){ /* 05-jan-2001 vcs */
    k="Exposure";
    switch(ImageInfo->ExposureProgram) {
      case 2:
        MA(k,NULL,"program (auto)");
        break;
      case 3:
        MA(k,NULL,"aperture priority (semi-auto)");
        break;
      case 4:
        MA(k,NULL,"shutter priority (semi-auto)");
        break;

      case 1:
        /* Quercus: 17-1-2004 The Kodak DX6340 (and may be others) use ExposureProgram number 1 for
           Shutter Priority program with times shorter than 0.5 seconds */
        MA(k,NULL,"shutter priority (semi-auto)");
        break;

        /* Some other programs from DX6340 */
      case 7:
        MA(k,NULL,"portrait (auto)");
        break;
      case 6:
        MA(k,NULL,"action (auto)");
        break;
      default:
        MA(k,NULL,"auto"); }}



  { int a;
    for(a=0;;a++) {
      if(ProcessTable[a].Tag==ImageInfo->Process || ProcessTable[a].Tag==0) {
        MA("JPEG process",NULL,ProcessTable[a].Desc);
        break; }}}



  /* Print the comment. Print 'Comment:' for each new line of comment. */
  if(ImageInfo->Comments[0])
    MA("Comment",NULL,ImageInfo->Comments);

  estring_dst(e); }



/* Summarize highlights of image info on one line (suitable for grep-ing) */
static void ShowConciseImageInfo(ImageInfo_t *ImageInfo,noir_provider *prov) {
  estring *e=estring_new(256);

  estring_printf(e,"%dx%d",ImageInfo->Width,ImageInfo->Height);

  if(ImageInfo->ExposureTime)
    estring_appendf(e," (1/%d)",(int)(0.5+1/ImageInfo->ExposureTime));

  if(ImageInfo->ApertureFNumber)
    estring_appendf(e," f/%3.1f",(double)ImageInfo->ApertureFNumber);

  if(ImageInfo->FocalLength35mmEquiv)
    estring_appendf(e," f(35)=%dmm",ImageInfo->FocalLength35mmEquiv);

  if(ImageInfo->FlashUsed>0)
    estring_appendf(e," (flash)");

  if(!ImageInfo->IsColor)
    estring_appendf(e," (bw)");

  if(estring_length(e))
    meta_add(ImageInfo->md,META_RO,META_STR,"sortkey","If you enable 'Sort by meta-data', files will be sorted according to this field.",MY_NAME,(char *)ESTRING_GET_CSTRING(e),prov);

  estring_dst(e); }



static int ProcessFile(noir_md *md,noir_provider *prov,const char *FileName) {
  ImageInfo_t ImageInfo;
  int         a;
  Section_t   Sections[MAX_SECTIONS];
  FILE       *infile;
  int         ret=NOIR_FAIL;

  memset(&Sections,0,sizeof(Sections));

  memset(&ImageInfo,0,sizeof(ImageInfo));
  ImageInfo.FlashUsed=-1;
  ImageInfo.MeteringMode=-1;
  ImageInfo.md=md;

  if(!(infile=fopen(FileName,"rb")))
    return NOIR_NOFILE;

  if(((ret=ReadJpegSections(&ImageInfo,Sections,infile))!=NOIR_SUCC)&&
     (ret!=NOIR_PARTIAL)) {
    E(0,"Not JPEG: %s\n",FileName);
    ret=NOIR_TYPEMISMATCH; }
  else
    ret=NOIR_SUCC;

  fclose(infile);

  ShowConciseImageInfo(&ImageInfo,prov);
  ShowImageInfo(&ImageInfo,prov);

  for(a=0;a<ImageInfo.SectionsRead;a++)
    free(Sections[a].Data);
  memset(&ImageInfo,0,sizeof(ImageInfo));

  return NOIR_SUCC; }



/*****************************************************************************/



/**
 * get meta-data for this file
 * @return an error-code
 * @param md    the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *md,char *file,noir_provider *prov) {
  return ProcessFile(md,prov,file); }
	


/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }
