/** @file  ipc_dcop.cpp
 *  @brief DCOP IPC module for evidence
 *
 * by Azundris
 */


#include <kapp.h>
#include <kuniqueapplication.h>
#include <kcmdlineargs.h>
#include <dcopclient.h>

#include "ipc_dcop.h"
#include "ipc_dcop.moc"



extern "C" {
#include "config.h"
#include "ipc.h"
#include "errors.h"

#include <stdlib.h>
}  // extern "C"






/*****************************************************************************/
/* constructor, destructor */



DCOPEvidenceWidget::DCOPEvidenceWidget()
    : DCOPEvidenceIface(),
      DCOPObject("evidence") {
  if(!kapp->dcopClient()->isRegistered()) {
    kapp->dcopClient()->registerAs("evidence");
    kapp->dcopClient()->setDefaultObject(objId()); }}



DCOPEvidenceWidget::~DCOPEvidenceWidget() {
}






/*****************************************************************************/
/* helper: return QCStringList(split /\n/,cstring); */



/**
 * convert \n separated lines into a QCStringList
 * @return a QCStringList
 * @param i  \n separated lines
 */
QCStringList c2QCStringList(char *i) {
  QCStringList o;
  char *p=i,*q;
  while((q=strchr(p,'\n'))) {
    *q='\0';
    o.append(p);
    p=q+1; }
  o.append(p);
  free(i);
  qHeapSort(o);
  return o; }



/**
 * wrap a C-string in a QString
 * @return a QString
 * @param i  a C-string to wrap and free()
 */
QString c2QString(char *i) {
  QString o=QString(i);
  free(i);
  return o; }






/*****************************************************************************/
/* member functions: Properties */



QCStringList DCOPEvidenceWidget::listProperties(long serial) const {
  QCStringList o=c2QCStringList(ipc_prop_list(serial));
  return o; }

int DCOPEvidenceWidget::setProperty(long serial,const QString &key,const QString &val) {
  return ipc_prop_set(serial,(char *)key.ascii(),(char *)val.ascii()); }

QString DCOPEvidenceWidget::getProperty(long serial,const QString &key) const {
  return c2QString(ipc_prop_get(serial,key.ascii())); }

QString DCOPEvidenceWidget::descProperty(long serial,const QString &key) const {
  return c2QString(ipc_prop_desc(serial,key.ascii())); }






/*****************************************************************************/
/* member functions: commands */



QCStringList DCOPEvidenceWidget::ls(const QString &file) const {
  QCStringList o=c2QCStringList(ipc_cmd_ls((char *)file.ascii()));
  return o; }

QCStringList DCOPEvidenceWidget::info(const QString &file) const {
  QCStringList o=c2QCStringList(ipc_cmd_info((char *)file.ascii()));
  return o; }






/*****************************************************************************/
/* member functions: windows */



QCStringList DCOPEvidenceWidget::listWindows() const {
  QCStringList o=c2QCStringList(ipc_win_list());
  return o; }

QString DCOPEvidenceWidget::getWindowSel(long serial) const {
  return c2QString(ipc_win_sel_get(serial)); }

int DCOPEvidenceWidget::setWindowSel(long serial,const QString &sel) const {
  return ipc_win_sel_set(serial,sel.ascii()); }

QString DCOPEvidenceWidget::getWindowCWD(long serial) const {
  return c2QString(ipc_win_cwd_get(serial)); }

int DCOPEvidenceWidget::setWindowCWD(long serial,const QString &cwd) const {
  return ipc_win_cwd_set(serial,cwd.ascii()); }

int DCOPEvidenceWidget::newWindow(const QString &cwd) const {
  return ipc_win_new(cwd.ascii()); }

int DCOPEvidenceWidget::getWindowByCWD(const QString &cwd) {
  return ipc_win_cwd_find(cwd.ascii()); }






/*****************************************************************************/



static int init_do(void **data) {
  KApplication **app=(KApplication **)data;
  char *argv[2];

  if(!app)
    return NOIR_FAIL;

  if(*app)
    return NOIR_IGNORED;

  argv[0]="evidence";
  argv[1]=NULL;
  KCmdLineArgs::init(1,(char **)&argv,"evidence","","");
  *app=new KApplication(false,false);

  DCOPEvidenceWidget *win=new DCOPEvidenceWidget();

  return NOIR_SUCC; }






/*****************************************************************************/
/* PUBLIC INTERFACE */
/********************/



extern "C" {

/**
 * initialize DCOP IPC module
 * @return an error-code
 * @param data  where to store our connection handle
 */
int ipc_module_init(void **data) {
  return init_do(data); }



/**
 * deinitialize DCOP IPC (destall handlers, drop connection, release resources)
 * @return an error-code
 * @param data  the connection to throw out
 */
int ipc_module_exit(void **data) {
  return NOIR_SUCC; }



/**
 * check for messages from DCOP
 * @return an error-code
 * @param data  the connection
 */
int ipc_module_poll(void **data) {
  KApplication **app=(KApplication **)data;

  if(!app)
    return NOIR_FAIL;

  (*app)->processEvents(10);

  return NOIR_SUCC; }

}
