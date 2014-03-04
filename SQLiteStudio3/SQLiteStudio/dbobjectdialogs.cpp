#include "dbobjectdialogs.h"
#include "mainwindow.h"
#include "dialogs/indexdialog.h"
#include "dialogs/triggerdialog.h"
#include "utils_sql.h"
#include "dbtree/dbtree.h"
#include "notifymanager.h"
#include "mdiarea.h"
#include "mdiwindow.h"
#include "windows/tablewindow.h"
#include "windows/viewwindow.h"
#include <QMessageBox>
#include <QDebug>

DbObjectDialogs::DbObjectDialogs(Db* db) :
    db(db)
{
    mainWindow = MainWindow::getInstance();
    mdiArea = mainWindow->getMdiArea();
    parentWidget = mainWindow;
}

DbObjectDialogs::DbObjectDialogs(Db* db, QWidget* parentWidget) :
    db(db), parentWidget(parentWidget)
{
    mainWindow = MainWindow::getInstance();
    mdiArea = mainWindow->getMdiArea();
}

void DbObjectDialogs::addIndex(const QString& table)
{
    IndexDialog dialog(db, parentWidget);
    if (!table.isNull())
        dialog.setTable(table);

    dialog.exec();
}

void DbObjectDialogs::editIndex(const QString& index)
{
    if (index.isNull())
    {
        qWarning() << "Tried to edit null index.";
        return;
    }

    IndexDialog dialog(db, index, parentWidget);
    dialog.exec();
}

void DbObjectDialogs::addTriggerOnTable(const QString& table)
{
    addTrigger(table, QString::null);
}

void DbObjectDialogs::addTriggerOnView(const QString& view)
{
    addTrigger(QString::null, view);
}

void DbObjectDialogs::addTrigger(const QString& table, const QString& view)
{
    TriggerDialog dialog(db, parentWidget);
    if (!table.isNull())
        dialog.setParentTable(table);
    else if (!view.isNull())
        dialog.setParentView(view);
    else
        return;

    dialog.exec();
}

void DbObjectDialogs::editTrigger(const QString& trigger)
{
    if (trigger.isNull())
    {
        qWarning() << "Tried to edit null trigger.";
        return;
    }

    TriggerDialog dialog(db, parentWidget);
    dialog.setTrigger(trigger);
    dialog.exec();
}

ViewWindow* DbObjectDialogs::addView()
{
    ViewWindow* win = new ViewWindow(db, mdiArea);
    mdiArea->addSubWindow(win);
    return win;
}

ViewWindow* DbObjectDialogs::editView(const QString& database, const QString& view)
{
    ViewWindow* win = nullptr;
    foreach (MdiWindow* mdiWin, mdiArea->getWindows())
    {
        win = dynamic_cast<ViewWindow*>(mdiWin->getMdiChild());
        if (!win)
            continue;

        if (win->getDb() == db && win->getView() == view)
        {
            mdiArea->setActiveSubWindow(mdiWin);
            return win;
        }
    }

    win = new ViewWindow(mdiArea, db, database, view);
    if (win->isInvalid())
    {
        delete win;
        return nullptr;
    }

    mdiArea->addSubWindow(win);
    return win;
}

void DbObjectDialogs::editObject(const QString& name)
{
    editObject("main", name);
}

void DbObjectDialogs::editObject(const QString& database, const QString& name)
{
    Type type = getObjectType(database, name);
    switch (type)
    {
        case Type::TABLE:
            editTable(database, name);
            break;
        case Type::INDEX:
            editIndex(name);
            break;
        case Type::TRIGGER:
            editTrigger(name);
            break;
        case Type::VIEW:
            editView(database, name);
            break;
        default:
        {
            qCritical() << "Unknown object type while trying to edit object. Object name:" << database << "." << name;
            return;
        }
    }
}

bool DbObjectDialogs::dropObject(const QString& name)
{
    return dropObject("main", name);
}

bool DbObjectDialogs::dropObject(const QString& database, const QString& name)
{
    static const QString dropSql = "DROP %1 %2.%3;";

    Dialect dialect = db->getDialect();
    QString dbName = wrapObjIfNeeded(database, dialect);

    Type type = getObjectType(database, name);
    QString title;
    QString message;
    QString typeForSql;
    switch (type)
    {
        case Type::TABLE:
            title = tr("Delete table");
            message = tr("Are you sure you want to delete table %1?");
            typeForSql = "TABLE";
            break;
        case Type::INDEX:
            title = tr("Delete index");
            message = tr("Are you sure you want to delete index %1?");
            typeForSql = "INDEX";
            break;
        case Type::TRIGGER:
            title = tr("Delete trigger");
            message = tr("Are you sure you want to delete trigger %1?");
            typeForSql = "TRIGGER";
            break;
        case Type::VIEW:
            title = tr("Delete view");
            message = tr("Are you sure you want to delete view %1?");
            typeForSql = "VIEW";
            break;
        default:
        {
            qCritical() << "Unknown object type while trying to drop object. Object name:" << database << "." << name;
            return false;
        }
    }

    QMessageBox::StandardButton resp = QMessageBox::question(parentWidget, title, message.arg(name));
    if (resp != QMessageBox::Yes)
        return false;

    SqlResultsPtr results = db->exec(dropSql, {typeForSql, dbName, wrapObjIfNeeded(name, dialect)}, Db::Flag::STRING_REPLACE_ARGS);
    if (results->isError())
    {
        notifyError(tr("Error while dropping %1: %2").arg(name).arg(results->getErrorText()));
        qCritical() << "Error while dropping object " << database << "." << name << ":" << results->getErrorText();
        return false;
    }

    DBTREE->refreshSchema(db);
    return true;
}

DbObjectDialogs::Type DbObjectDialogs::getObjectType(const QString& database, const QString& name)
{
    static const QString typeSql = "SELECT type FROM %1.sqlite_master WHERE name = :name;";
    static const QStringList types = {"table", "index", "trigger", "view"};

    Dialect dialect = db->getDialect();
    QString dbName = wrapObjIfNeeded(database, dialect);
    SqlResultsPtr results = db->exec(typeSql.arg(dbName), {name});
    if (results->isError())
    {
        qCritical() << "Could not get object type. Object name:" << database << "." << name << ", error:"
                    << results->getErrorText();
        return Type::UNKNOWN;
    }

    QString typeStr = results->getSingleCell().toString();
    return static_cast<Type>(types.indexOf(typeStr));
}

TableWindow* DbObjectDialogs::editTable(const QString& database, const QString& table)
{
    TableWindow* win = nullptr;
    foreach (MdiWindow* mdiWin, mdiArea->getWindows())
    {
        win = dynamic_cast<TableWindow*>(mdiWin->getMdiChild());
        if (!win)
            continue;

        if (win->getDb() == db && win->getTable() == table)
        {
            mdiArea->setActiveSubWindow(mdiWin);
            return win;
        }
    }

    win = new TableWindow(mdiArea, db, database, table);
    if (win->isInvalid())
    {
        delete win;
        return nullptr;
    }

    mdiArea->addSubWindow(win);
    return win;
}

TableWindow* DbObjectDialogs::addTable()
{
    TableWindow* win = new TableWindow(db, mdiArea);
    mdiArea->addSubWindow(win);
    return win;
}