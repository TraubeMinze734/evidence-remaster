/** @file  monitors.h
 *  @brief file-monitors (notice new/changed/deleted items in file-system)
 */

#ifndef MONITORS_H
#  define MONITORS_H



/** monitor-events */
typedef enum {
  NOIR_MONITOR_DK,               /**< unspecified type */
  NOIR_MONITOR_CHG,              /**< object has changed */
  NOIR_MONITOR_DEL,              /**< object was deleted */
  NOIR_MONITOR_EXECSTART,        /**< item was opened */
  NOIR_MONITOR_EXECSTOP,         /**< item was closed */
  NOIR_MONITOR_NEW,              /**< object was created */
  NOIR_MONITOR_META,             /**< meta-data of object have changed */
  NOIR_MONITOR_RLD,              /**< directory-contents have changed in unspecified way (backend's monitors are kinda dumb), rescan all items in directory and issue per-item events to model as needed */
  NOIR_MONITOR_EVENTS } noir_file_event;



/** monitor-types */
typedef enum {
  NOIR_MONITOR_DIR,              /**< a monitor for a directory */
  NOIR_MONITOR_FILE,             /**< a monitor for a file */
  NOIR_MONITOR_TYPES } noir_file_monitor;



/** a monitor (abstract) */
typedef struct _noir_monitor {
  char                 *want;    /**< the object we're interested in */
  char                 *have;    /**< the deepest item in 'want' that actually exists in the file-system (if want==/someuser/some.cfg and some.cfg doesn't exist, have==/someuser until some.cfg becomes availabe, at which point have==/someuser/some.cfg) */
  noir_file_monitor     type;    /**< what sort of object to check on */
  void                 *session; /**< some tag for the front-end to recognize
                                      the owner of this monitor by */
  void                 *handle;  /**< some tag for the back-end to recgonize
                                      this monitor by */
  const char           *system;  /**< if a backend supports different ways to
                                      monitor an object (FAM, F_NOTIFY, poll),
                                      it may note the mon-system here */
  void                 *data,    /**< the backend can store something here */
                       *cb_data; /**< the frontend can store something here */
  time_t                ts;      /**< time-stamp (when the object last changed) */
  time_t                ts2;     /**< time-stamp (when the object last updated in front-end) */
  int                 (*file_event_handler)(void *session,file_info *fi,model_event ev,file_flags fc,void *data);  /* callback */
  struct _noir_monitor *next; } noir_monitor;



noir_monitor *file_monitor_get_1st(void);
int           file_monitor_event(noir_monitor *,noir_file_event,char *);
noir_monitor *file_monitor_exists(void *session,char *,noir_monitor **);
int           file_monitor_add_with_handler(void *session,noir_file_monitor,char *,int (*file_event_handler)(void *session,file_info *fi,model_event ev,file_flags fc,void *data),void *data);
int           file_monitor_add(void *session,noir_file_monitor,char *);
int           file_monitor_remove(void *session,char *);
int           file_monitor_remove_all(void *session);



#endif
