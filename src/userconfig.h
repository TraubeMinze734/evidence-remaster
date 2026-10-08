/** @file  userconfig.h
 *  @brief user-definables.
 *
 * factory defaults are set for maximum comfort and minimum confusion to new
 * users; sometimes at a slight speed penalty.
 * most of these will become run-time configurables at a later date.
 */



/** CACHE_SLOWER_THAN
    if a directory takes longer than this many miliseconds to process
    (read, stat, mime, meta, thumbnail, and whatever else), create a
    persistent cache in the user's home-dir so it will load faster
    next time.  set to 0 to disable caching altogether. */
#define CACHE_SLOWER_THAN 2500



/** SELECTION_PARALLEL
    if several files are selected, display info for them one after the other,
    or all at once, opening all file-dials at once? */
#undef SELECTION_PARALLEL



/** FS_SETS_X_ON_EVERYTHING
    undef'd:  if "x" bits ("executable") are set on a file, we consider
              it an executable and never bother with MIME-lookups
              by file-extension and the like.  slightly faster, but
              will result in everything looking like an executable
              when mounting win-partitions or certain CD-Rs.
    defined:  slightly slower, but it will make us look ever so much
              smarter on imports from Windows and the like
              where the "executable" bits are set on everything. */
#define FS_SETS_X_ON_EVERYTHING



/** HIDE_FILES_STARTING_WITH_HASH
    defined:  hide files whose name starts with a #.  this way,
              the temporary files of emacsen will be placed in
              the (virtual) folder ".files", together with other
              temporary, backup and hidden files, as to unclutter
              the main view.
    undef'd:  places files whose name starts with a # with all
              other files.  as xchat is the only other program
              I know of that creates such files (IRC channel-logs,
              where by default the first part of the file's name
              is the channel's), this is probably only of interest
              if you do use xchat, but don't use emacs, or if you
              access your logs really a lot. */
#undef HIDE_FILES_STARTING_WITH_HASH



/** THUMBNAIL_IMAGES_SMALLER_THAN
    evidence will not attempt to create thumbnails for files larger
    than this size (in bytes).  this will speed up the icon-view.
    the "tooltip" (balloon info) remains unaffected by this, and
    will always try to show a preview.  set to 0 for no limit. */
#define THUMBNAIL_IMAGES_SMALLER_THAN 0L



/** WITH_DOT_DIR
    defined:  put all "hidden" files in a virtual folder
    undef'd:  show "hidden" files, no special treatmeant */
#define WITH_DOT_DIR



/** FONTTHUMB_TRANSLUCENT
    font thumbs look much nicer when slightly translucent, but it's just
    wrong semantically, as normally, translucency indicates a symlink in
    some icon-factories.  IOW, translucency should be provided by the icon-
    factory (for whatever reason), not by the thumbnail factory.
    defined:  make translucent, anyway
    undef'd:  keep GUI consistent */
#undef FONTTHUMB_TRANSLUCENT



/** ICONLIST_FIND_ICON_BY_STRING_RESETS_AFTER
    when entering the beginning of an icon's label, we jump to that icon.
    after not entering a character in "x" seconds, reset that string. */
#define ICONLIST_FIND_ICON_BY_STRING_RESETS_AFTER 2



/** PNG_COMPRESS
    when writing PNG images, use this compression level (9 == highest) */
#define PNG_COMPRESS 9



#define ENUM_GROUPS
#define ENUM_TIMES
