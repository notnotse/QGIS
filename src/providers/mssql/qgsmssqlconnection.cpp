/***************************************************************************
                             qgsmssqlconnection.cpp
                             ----------------------
    begin                : October 2018
    copyright            : (C) 2018 by Nyall Dawson
    email                : nyall dot dawson at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsmssqlconnection.h"
#include "qgslogger.h"
#include "qgssettings.h"
#include "qgsdatasourceuri.h"
#include <QSqlDatabase>
#include <QThread>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>

int QgsMssqlConnection::sConnectionId = 0;

QSqlDatabase QgsMssqlConnection::getDatabase( const QString &service, const QString &host, const QString &database, const QString &username, const QString &password )
{
  QSqlDatabase db;
  QString connectionName;

  // create a separate database connection for each feature source
  if ( service.isEmpty() )
  {
    if ( !host.isEmpty() )
      connectionName = host + '.';

    if ( database.isEmpty() )
    {
      QgsDebugMsg( QStringLiteral( "QgsMssqlProvider database name not specified" ) );
      return db;
    }

    connectionName += QStringLiteral( "%1.%2" ).arg( database ).arg( sConnectionId++ );
  }
  else
    connectionName = service;

  const QString threadSafeConnectionName = dbConnectionName( connectionName );
  if ( !QSqlDatabase::contains( threadSafeConnectionName ) )
  {
    db = QSqlDatabase::addDatabase( QStringLiteral( "QODBC" ), threadSafeConnectionName );
    db.setConnectOptions( QStringLiteral( "SQL_ATTR_CONNECTION_POOLING=SQL_CP_ONE_PER_HENV" ) );
  }
  else
    db = QSqlDatabase::database( threadSafeConnectionName );

  db.setHostName( host );
  QString connectionString;
  if ( !service.isEmpty() )
  {
    // driver was specified explicitly
    connectionString = service;
  }
  else
  {
#ifdef Q_OS_WIN
    connectionString = "driver={SQL Server}";
#else
    connectionString = QStringLiteral( "driver={FreeTDS};port=1433" );
#endif
  }

  if ( !host.isEmpty() )
    connectionString += ";server=" + host;

  if ( !database.isEmpty() )
    connectionString += ";database=" + database;

  if ( password.isEmpty() )
    connectionString += QLatin1String( ";trusted_connection=yes" );
  else
    connectionString += ";uid=" + username + ";pwd=" + password;

  if ( !username.isEmpty() )
    db.setUserName( username );

  if ( !password.isEmpty() )
    db.setPassword( password );

  db.setDatabaseName( connectionString );

  // only uncomment temporarily -- it can show connection password otherwise!
  // QgsDebugMsg( connectionString );
  return db;
}

bool QgsMssqlConnection::openDatabase( QSqlDatabase &db )
{
  if ( !db.isOpen() )
  {
    if ( !db.open() )
    {
      return false;
    }
  }
  return true;
}

bool QgsMssqlConnection::geometryColumnsOnly( const QString &name )
{
  QgsSettings settings;
  return settings.value( "/MSSQL/connections/" + name + "/geometryColumnsOnly", false ).toBool();
}

void QgsMssqlConnection::setGeometryColumnsOnly( const QString &name, bool enabled )
{
  QgsSettings settings;
  settings.setValue( "/MSSQL/connections/" + name + "/geometryColumnsOnly", enabled );
}

bool QgsMssqlConnection::allowGeometrylessTables( const QString &name )
{
  QgsSettings settings;
  return settings.value( "/MSSQL/connections/" + name + "/allowGeometrylessTables", false ).toBool();
}

void QgsMssqlConnection::setAllowGeometrylessTables( const QString &name, bool enabled )
{
  QgsSettings settings;
  settings.setValue( "/MSSQL/connections/" + name + "/allowGeometrylessTables", enabled );
}

bool QgsMssqlConnection::useEstimatedMetadata( const QString &name )
{
  QgsSettings settings;
  return settings.value( "/MSSQL/connections/" + name + "/estimatedMetadata", false ).toBool();
}

void QgsMssqlConnection::setUseEstimatedMetadata( const QString &name, bool enabled )
{
  QgsSettings settings;
  settings.setValue( "/MSSQL/connections/" + name + "/estimatedMetadata", enabled );
}

bool QgsMssqlConnection::isInvalidGeometryHandlingDisabled( const QString &name )
{
  QgsSettings settings;
  return settings.value( "/MSSQL/connections/" + name + "/disableInvalidGeometryHandling", false ).toBool();
}

void QgsMssqlConnection::setInvalidGeometryHandlingDisabled( const QString &name, bool disabled )
{
  QgsSettings settings;
  settings.setValue( "/MSSQL/connections/" + name + "/disableInvalidGeometryHandling", disabled );
}

bool QgsMssqlConnection::dropTable( const QString &uri, QString *errorMessage )
{
  QgsDataSourceUri dsUri( uri );

  // connect to database
  QSqlDatabase db = getDatabase( dsUri.service(), dsUri.host(), dsUri.database(), dsUri.username(), dsUri.password() );
  const QString schema = dsUri.schema();
  const QString table = dsUri.table();

  if ( !openDatabase( db ) )
  {
    if ( errorMessage )
      *errorMessage = db.lastError().text();
    return false;
  }

  QSqlQuery q = QSqlQuery( db );
  q.setForwardOnly( true );
  const QString sql = QString( "IF EXISTS (SELECT * FROM sys.objects WHERE object_id = OBJECT_ID(N'[%1].[%2]') AND type in (N'U')) DROP TABLE [%1].[%2]\n"
                               "DELETE FROM geometry_columns WHERE f_table_schema = '%1' AND f_table_name = '%2'" )
                      .arg( schema,
                            table );
  if ( !q.exec( sql ) )
  {
    if ( errorMessage )
      *errorMessage = q.lastError().text();
    return false;
  }

  return true;
}

bool QgsMssqlConnection::truncateTable( const QString &uri, QString *errorMessage )
{
  QgsDataSourceUri dsUri( uri );

  // connect to database
  QSqlDatabase db = getDatabase( dsUri.service(), dsUri.host(), dsUri.database(), dsUri.username(), dsUri.password() );
  const QString schema = dsUri.schema();
  const QString table = dsUri.table();

  if ( !openDatabase( db ) )
  {
    if ( errorMessage )
      *errorMessage = db.lastError().text();
    return false;
  }

  QSqlQuery q = QSqlQuery( db );
  q.setForwardOnly( true );
  const QString sql = QStringLiteral( "TRUNCATE TABLE [%1].[%2]" ).arg( schema, table );
  if ( !q.exec( sql ) )
  {
    if ( errorMessage )
      *errorMessage = q.lastError().text();
    return false;
  }

  return true;
}

bool QgsMssqlConnection::createSchema( const QString &uri, const QString &schemaName, QString *errorMessage )
{
  QgsDataSourceUri dsUri( uri );

  // connect to database
  QSqlDatabase db = getDatabase( dsUri.service(), dsUri.host(), dsUri.database(), dsUri.username(), dsUri.password() );

  if ( !openDatabase( db ) )
  {
    if ( errorMessage )
      *errorMessage = db.lastError().text();
    return false;
  }

  QSqlQuery q = QSqlQuery( db );
  q.setForwardOnly( true );
  const QString sql = QStringLiteral( "CREATE SCHEMA [%1]" ).arg( schemaName );
  if ( !q.exec( sql ) )
  {
    if ( errorMessage )
      *errorMessage = q.lastError().text();
    return false;
  }

  return true;
}

QStringList QgsMssqlConnection::schemas( const QString &uri, QString *errorMessage )
{
  QgsDataSourceUri dsUri( uri );

// connect to database
  QSqlDatabase db = getDatabase( dsUri.service(), dsUri.host(), dsUri.database(), dsUri.username(), dsUri.password() );

  if ( !openDatabase( db ) )
  {
    if ( errorMessage )
      *errorMessage = db.lastError().text();
    return QStringList();
  }

  const QString sql = QStringLiteral( "select s.name as schema_name from sys.schemas s" );

  QSqlQuery q = QSqlQuery( db );
  q.setForwardOnly( true );
  if ( !q.exec( sql ) )
  {
    if ( errorMessage )
      *errorMessage = q.lastError().text();
    return QStringList();
  }

  QStringList result;

  while ( q.next() )
  {
    const QString schemaName = q.value( 0 ).toString();
    result << schemaName;
  }
  return result;
}

bool QgsMssqlConnection::isSystemSchema( const QString &schema )
{
  static QSet< QString > sSystemSchemas
  {
    QStringLiteral( "db_owner" ),
    QStringLiteral( "db_securityadmin" ),
    QStringLiteral( "db_accessadmin" ),
    QStringLiteral( "db_backupoperator" ),
    QStringLiteral( "db_ddladmin" ),
    QStringLiteral( "db_datawriter" ),
    QStringLiteral( "db_datareader" ),
    QStringLiteral( "db_denydatawriter" ),
    QStringLiteral( "db_denydatareader" ),
    QStringLiteral( "INFORMATION_SCHEMA" ),
    QStringLiteral( "sys" )
  };

  return sSystemSchemas.contains( schema );
}

QString QgsMssqlConnection::dbConnectionName( const QString &name )
{
  // Starting with Qt 5.11, sharing the same connection between threads is not allowed.
  // We use a dedicated connection for each thread requiring access to the database,
  // using the thread address as connection name.
  const QString threadAddress = QStringLiteral( ":0x%1" ).arg( QString::number( reinterpret_cast< quintptr >( QThread::currentThread() ), 16 ) );
  return name + threadAddress;
}
