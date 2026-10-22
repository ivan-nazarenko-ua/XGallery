#ifndef MIGRATIONS_H
#define MIGRATIONS_H

#include <QSqlDatabase>
#include <QString>

// Brings the database schema up to date, one version at a time.
// The current version is stored in PRAGMA user_version (0 = empty or pre-galleries DB).
//
// To change the schema: add a new step function in migrations.cpp, append it to the
// step list there and never edit existing steps — users' databases may be at any version.
class Migrations
{
public:
    struct Step; // defined in migrations.cpp

    static int latestVersion();

    // Runs all pending steps, each in its own transaction. On failure the failed step is
    // rolled back, *error is filled and false is returned.
    static bool run(QSqlDatabase &db, QString *error);

private:
    static bool runStep(QSqlDatabase &db, const Step &step, int toVersion, QString *error);
};

#endif // MIGRATIONS_H
