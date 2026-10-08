/* by Azundris */

#ifndef DCOP_EVIDENCE
#  define DCOP_EVIDENCE
#  include "ipc_dcop_iface.h"

class DCOPEvidenceWidget : public QObject, virtual public DCOPEvidenceIface
{
    Q_OBJECT

public:
    DCOPEvidenceWidget();
    ~DCOPEvidenceWidget();

public slots:
    QCStringList listProperties(long serial) const;
    QString      descProperty(long serial,const QString &key) const;
    QString      getProperty(long serial,const QString &key) const;
    int          setProperty(long serial,const QString &key,const QString &val);

    QCStringList ls(const QString &dir) const;
    QCStringList info(const QString &file) const;

    QCStringList listWindows()  const;
    QString      getWindowCWD(long serial) const;
    QString      getWindowSel(long serial) const;
    int          setWindowSel(long serial,const QString &sel) const;
    int          setWindowCWD(long serial,const QString &cwd) const;
    int          getWindowByCWD(const QString &cwd);
    int          newWindow(const QString &cwd) const;
};
#endif
