/* by Azundris */

#ifndef DCOP_EVIDENCE_IFACE
#  define DCOP_EVIDENCE_IFACE
#  include <dcopobject.h>

class DCOPEvidenceIface : virtual public DCOPObject
{
  K_DCOP
k_dcop:

  virtual QCStringList listProperties(long serial) const = 0;
  virtual QString      descProperty(long serial,const QString &key) const = 0;
  virtual QString      getProperty(long serial,const QString &key) const = 0;
  virtual int          setProperty(long serial,const QString &key,const QString &val) = 0;

  virtual QCStringList ls(const QString &dir) const = 0;
  virtual QCStringList info(const QString &file) const = 0;

  virtual QCStringList listWindows() const = 0;
  virtual QString      getWindowCWD(long serial) const = 0;
  virtual QString      getWindowSel(long serial) const = 0;
  virtual int          setWindowSel(long serial,const QString &sel) const = 0;
  virtual int          setWindowCWD(long serial,const QString &cwd) const = 0;
  virtual int          getWindowByCWD(const QString &cwd) = 0;
  virtual int          newWindow(const QString &cwd) const = 0;
};

#endif
