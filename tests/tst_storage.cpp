#include <QTest>
#include <QDebug>
#include <QTimeZone>
#include <KDebug>
#include <ksystemtimezone.h>
#include <icalformat.h>

#include "tst_storage.h"
#include "sqlitestorage.h"

// random
const char *const NotebookId("12345678-9876-1111-2222-222222222222");


tst_storage::tst_storage(QObject *parent)
    : QObject(parent)
{
}

void tst_storage::initTestCase()
{
    openDb(true);
}

void tst_storage::cleanupTestCase()
{
    mKCal::Notebook::Ptr notebook = m_storage->notebook(NotebookId);
    m_storage->deleteNotebook(notebook);
}

void tst_storage::init()
{
}

void tst_storage::cleanup()
{
}

void tst_storage::tst_timezone()
{
    // for test sanity, verify kdatetime actually agrees timezone is for helsinki.
    KDateTime localTime(QDate(2014, 1, 1), KSystemTimeZones::zone("Europe/Helsinki"));
    QCOMPARE(localTime.utcOffset(), 7200);
}

void tst_storage::tst_allday_data()
{
    QTest::addColumn<QDate>("startDate");
    QTest::addColumn<int>("days");

    // DST changes according to finnish timezone
    // normal 1 day events
    QTest::newRow("normal") << QDate(2013, 10, 10) << 0;
    QTest::newRow("to non-DST") << QDate(2013, 10, 27) << 0;
    QTest::newRow("to DST") << QDate(2013, 3, 31) << 0;

    // 2 day events
    QTest::newRow("normal, 2d") << QDate(2013, 10, 10) << 1;
    QTest::newRow("to non-DST, 2d") << QDate(2013, 10, 27) << 1;
    QTest::newRow("to DST, 2d") << QDate(2013, 3, 31) << 1;
}

void tst_storage::tst_allday()
{
    auto event = KCalCore::Event::Ptr(new KCalCore::Event);

    QFETCH(QDate, startDate);
    QFETCH(int, days);

    event->setDtStart(KDateTime(startDate, QTime(), KDateTime::ClockTime));
    event->setAllDay(true);
    if (days) {
        event->setDtEnd(KDateTime(startDate.addDays(days), QTime(), KDateTime::ClockTime));
    }
    event->setSummary("test event");

    QCOMPARE(event->allDay(), true);
    QCOMPARE(event->dtStart().date(), startDate);

    if (days) {
        QCOMPARE(event->dateEnd(), startDate.addDays(days));
        QCOMPARE(event->hasEndDate(), true);
        QVERIFY(event->dateEnd() > event->dtStart().date());
    } else {
        QCOMPARE(event->dateEnd(), startDate);
        QCOMPARE(event->hasEndDate(), false);
    }

    m_calendar->addEvent(event, NotebookId);
    m_storage->save();
    QString uid = event->uid();
    reloadDb();

    auto fetchedEvent = m_calendar->event(uid);
    QVERIFY(fetchedEvent.data());
    QCOMPARE(fetchedEvent->allDay(), true);
    QCOMPARE(fetchedEvent->dtStart().date(), startDate);
    QTime time = fetchedEvent->dtStart().time();
    QVERIFY(time == QTime() || time == QTime(0, 0));

    QTime localTime = fetchedEvent->dtStart().toLocalZone().time();
    QVERIFY(localTime == QTime() || localTime == QTime(0, 0));

    if (days) {
        QCOMPARE(fetchedEvent->dateEnd(), startDate.addDays(days));
        QCOMPARE(fetchedEvent->hasEndDate(), true);
        QVERIFY(event->dateEnd() > event->dtStart().date());
    } else {
        QCOMPARE(fetchedEvent->dateEnd(), startDate);
        QCOMPARE(fetchedEvent->hasEndDate(), false);
    }
}

void tst_storage::tst_alldayUtc()
{
    // test event saved with UTC time
    auto event = KCalCore::Event::Ptr(new KCalCore::Event);
    QDate startDate(2013, 12, 1);
    event->setDtStart(KDateTime(startDate, QTime(), KDateTime::UTC));
    event->setAllDay(true);
    event->setSummary("test event utc");

    QCOMPARE(event->allDay(), true);
    QCOMPARE(event->dtStart().timeType(), KDateTime::UTC);

    m_calendar->addEvent(event, NotebookId);
    m_storage->save();
    QString uid = event->uid();
    reloadDb();

    auto fetchedEvent = m_calendar->event(uid);
    QVERIFY(fetchedEvent.data());
    QVERIFY(fetchedEvent->dtStart().isUtc());

    KDateTime localStart = fetchedEvent->dtStart().toZone(KSystemTimeZones::zone("Europe/Helsinki"));
    QVERIFY(localStart.time() == QTime(2, 0));

    KDateTime localEnd = fetchedEvent->dtEnd().toZone(KSystemTimeZones::zone("Europe/Helsinki"));
    QVERIFY(localEnd.time() == QTime(2, 0));

    QCOMPARE(localEnd.date(), localStart.date().addDays(1));
}

// Verify that a recurring all day event is kept by storage
void tst_storage::tst_alldayRecurrence()
{
    auto event = KCalCore::Event::Ptr(new KCalCore::Event);

    QDate startDate(2013, 12, 1);
    event->setDtStart(KDateTime(startDate, QTime(), KDateTime::ClockTime));
    event->setAllDay(true);

    KCalCore::Recurrence *recurrence = event->recurrence();
    recurrence->setWeekly(1);
    recurrence->setStartDateTime(event->dtStart());
    recurrence->setAllDay(true);

    m_calendar->addEvent(event, NotebookId);
    m_storage->save();
    QString uid = event->uid();
    reloadDb();

    auto fetchEvent = m_calendar->event(uid);
    QVERIFY(fetchEvent);
    KCalCore::Recurrence *fetchRecurrence = fetchEvent->recurrence();
    QVERIFY(fetchRecurrence);
    QCOMPARE(*recurrence, *fetchRecurrence);
    KDateTime match = recurrence->getNextDateTime(KDateTime(startDate));
    QCOMPARE(match, KDateTime(startDate.addDays(7), QTime(), KDateTime::ClockTime));
}

// Verify that a recurrence with an exception rule is properly saved
void tst_storage::tst_recurrence()
{
    auto event = KCalCore::Event::Ptr(new KCalCore::Event);

    QDate startDate(2013, 12, 1);
    QTime startTime(12, 34, 56);
    event->setDtStart(KDateTime(startDate, startTime, KDateTime::ClockTime));

    KCalCore::Recurrence *recurrence = event->recurrence();
    recurrence->setDaily(1);
    KCalCore::RecurrenceRule *rrule = new KCalCore::RecurrenceRule;
    rrule->setRecurrenceType(KCalCore::RecurrenceRule::rWeekly);
    rrule->setDuration(5);
    recurrence->addExRule(rrule);
    recurrence->setStartDateTime(event->dtStart());

    m_calendar->addEvent(event, NotebookId);
    m_storage->save();
    QString uid = event->uid();
    reloadDb();

    auto fetchEvent = m_calendar->event(uid);
    QVERIFY(fetchEvent);
    KCalCore::Recurrence *fetchRecurrence = fetchEvent->recurrence();
    QVERIFY(fetchRecurrence);
    QCOMPARE(*recurrence, *fetchRecurrence);
    KDateTime match = recurrence->getNextDateTime(event->dtStart());
    QCOMPARE(match, event->dtStart().addDays(1));
}

void tst_storage::tst_origintimes()
{
    SqliteStorage *ss = dynamic_cast<SqliteStorage *>(m_storage.data());
    QVERIFY(ss);

    KDateTime utcTime(QDate(2014, 1, 15), QTime(), KDateTime::UTC);
    KDateTime clockTime(QDate(2014, 1, 15), QTime(), KDateTime::ClockTime);
    KDateTime localTime(QDate(2014, 1, 15), QTime(), KDateTime::LocalZone);

    // local origin time is the same as specific time set to utc
    // note: currently origin time of clock time is saved as time in current time zone.
    // that does not necessarily make sense, but better be careful when changing behavior there.
    QCOMPARE(ss->toOriginTime(utcTime), ss->toLocalOriginTime(utcTime));
    QCOMPARE(ss->toLocalOriginTime(clockTime), ss->toLocalOriginTime(utcTime));
    QCOMPARE(ss->toLocalOriginTime(localTime), ss->toLocalOriginTime(utcTime));
}

void tst_storage::tst_rawEvents_data()
{
    QTest::addColumn<QDate>("date");
    QTest::addColumn<QTime>("startTime");
    QTest::addColumn<QTime>("endTime");
    QTest::addColumn<QString>("timeZone");

    QTest::newRow("non all day event in clock time")
        << QDate(2010, 01, 01)
        << QTime(12, 0) << QTime(13, 0)
        << QString();
    QTest::newRow("non all day event in Europe/Helsinki")
        << QDate(2010, 02, 01)
        << QTime(12, 0) << QTime(13, 0)
        << QString("Europe/Helsinki");
    QTest::newRow("non all day event in Pacific/Midway")
        << QDate(2010, 03, 01)
        << QTime(8, 0) << QTime(9, 0)
        << QString("Pacific/Midway");
    QTest::newRow("all day event stored as local clock")
        << QDate(2010, 04, 01)
        << QTime(0, 0) << QTime()
        << QString();
    QTest::newRow("all day event stored as date only")
        << QDate(2010, 05, 01)
        << QTime() << QTime()
        << QString();
}

void tst_storage::tst_rawEvents()
{
    QFETCH(QDate, date);
    QFETCH(QTime, startTime);
    QFETCH(QTime, endTime);
    QFETCH(QString, timeZone);

    KDateTime::Spec spec(timeZone.isEmpty() ? KDateTime::Spec(KDateTime::ClockTime)
                         : KDateTime::Spec(KSystemTimeZones::zone(timeZone)));
    auto event = KCalCore::Event::Ptr(new KCalCore::Event);
    if (startTime.isValid()) {
        event->setDtStart(KDateTime(date, startTime, spec));
        if (endTime.isValid()) {
            event->setDtEnd(KDateTime(date, endTime, spec));
        } else if (startTime == QTime(0, 0)) {
            event->setAllDay(true);
        }
    } else {
        event->setDtStart(KDateTime(date, KDateTime::ClockTime));
    }
    event->setSummary(QStringLiteral("testing rawExpandedEvents()"));

    KCalCore::Recurrence *recurrence = event->recurrence();
    recurrence->setDaily(1);
    recurrence->setStartDateTime(event->dtStart());
    recurrence->setDuration(5);
    recurrence->setAllDay(event->allDay());
    if (event->dtStart().isDateOnly()) {
        // Save exception as clock time
        recurrence->addExDateTime(KDateTime(event->dtStart().date().addDays(1), QTime(0,0), KDateTime::ClockTime));
        // Save exception as a local zone
        recurrence->addExDateTime(KDateTime(event->dtStart().date().addDays(2), QTime(0,0)));
    } else {
        // Register an exception in spec of the event
        recurrence->addExDateTime(event->dtStart().addDays(1));
        // Register an exception in another time zone
        recurrence->addExDateTime(event->dtStart().addDays(2).toTimeSpec(KSystemTimeZones::zone("America/Toronto")));
    }

    m_calendar->addEvent(event, NotebookId);
    m_storage->save();
    QString uid = event->uid();
    reloadDb();

    auto fetchEvent = m_calendar->event(uid);
    QVERIFY(fetchEvent);
    QCOMPARE(fetchEvent->allDay(), event->allDay());
    KCalCore::Recurrence *fetchRecurrence = fetchEvent->recurrence();
    QVERIFY(fetchRecurrence);
    QCOMPARE(fetchRecurrence->allDay(), recurrence->allDay());

    // should return occurrence for both days and omit exceptions
    mKCal::ExtendedCalendar::ExpandedIncidenceList events
        = m_calendar->rawExpandedEvents(date, date.addDays(3), false, false, KDateTime::Spec(KDateTime::LocalZone));

    QCOMPARE(events.size(), 2);
    QCOMPARE(events[0].first.dtStart, event->dtStart().toLocalZone().dateTime());
    QCOMPARE(events[0].first.dtEnd, event->dtEnd().toLocalZone().dateTime());
    QCOMPARE(events[1].first.dtStart, event->dtStart().addDays(3).toLocalZone().dateTime());
    QCOMPARE(events[1].first.dtEnd, event->dtEnd().addDays(3).toLocalZone().dateTime());
}

// Check that the creation date can be tuned and restored properly.
void tst_storage::tst_dateCreated_data()
{
    QTest::addColumn<QDateTime>("dateCreated");
    QTest::addColumn<QDateTime>("dateCreated_update");

    QTest::newRow("insert new event without creation date")
        << QDateTime()
        << QDateTime();
    QTest::newRow("insert new event with creation date")
        << QDateTime::fromString("2019-04-01T10:21:15+02:00", Qt::ISODate)
        << QDateTime();
    QTest::newRow("update new event without creation date")
        << QDateTime()
        << QDateTime::fromString("2019-04-01T10:21:15+02:00", Qt::ISODate);
    QTest::newRow("update new event with creation date")
        << QDateTime::fromString("2019-04-01T10:21:15+02:00", Qt::ISODate)
        << QDateTime::fromString("2020-04-01T10:21:15+02:00", Qt::ISODate);
}

void tst_storage::tst_dateCreated()
{
    QFETCH(QDateTime, dateCreated);
    QFETCH(QDateTime, dateCreated_update);

    // Verify that date craetion date can be tuned on new insertion and on update.
    auto event = KCalCore::Event::Ptr(new KCalCore::Event);
    event->setDtStart(KDateTime(QDate(2019, 04, 01), QTime(10, 11),
                                KDateTime::ClockTime));
    event->setSummary("Creation date test event");
    event->setCreated(KDateTime(dateCreated.toUTC(), KDateTime::Spec::UTC()));

    m_calendar->addEvent(event, NotebookId);
    m_storage->save();
    reloadDb();

    auto fetchEvent = m_calendar->event(event->uid());
    QVERIFY(fetchEvent);
    if (dateCreated.isNull()) {
        QVERIFY(fetchEvent->created().secsTo(KDateTime::currentUtcDateTime()) <= 1);
    } else {
        QCOMPARE(fetchEvent->created().dateTime(), dateCreated);
    }

    if (!dateCreated_update.isNull()) {
        fetchEvent->setCreated(KDateTime(dateCreated_update.toUTC(),
                                         KDateTime::Spec::UTC()));
        fetchEvent->updated();
        m_storage->save();
        reloadDb();

        fetchEvent = m_calendar->event(event->uid());
        QVERIFY(fetchEvent);
        QCOMPARE(fetchEvent->created().dateTime(), dateCreated_update);
    }
}

// Check that lastModified field is not modified by storage,
// but actually updated whenever a modification is done to a stored incidence.
void tst_storage::tst_lastModified()
{
    KDateTime dt(QDate(2019, 07, 26), QTime(11, 41), KDateTime::ClockTime);
    KCalCore::Event::Ptr event = KCalCore::Event::Ptr(new KCalCore::Event);
    event->setDtStart(dt.addDays(1));
    event->setSummary("Modified date test event");
    event->setLastModified(dt);

    m_calendar->addEvent(event, NotebookId);
    m_storage->save();
    QCOMPARE(event->lastModified(), dt);

    reloadDb();
    auto fetchEvent = m_calendar->event(event->uid());
    QVERIFY(fetchEvent);
    QCOMPARE(fetchEvent->lastModified(), dt);

    fetchEvent->setDtStart(dt.addDays(2));
    QVERIFY(fetchEvent->lastModified().secsTo(KDateTime::currentUtcDateTime()) <= 1);
}

// Ensure that dissociateSingleOccurrence() for events
// given in various time zone or for all day events.
void tst_storage::tst_dissociateSingleOccurrence_data()
{
    QTest::addColumn<QDateTime>("dateTime");

    QTest::newRow("local time")
        << QDateTime(QDate(2019, 05, 21), QTime(12, 0), Qt::LocalTime);
    QTest::newRow("UTC time")
        << QDateTime(QDate(2019, 05, 21), QTime(12, 0), Qt::UTC);
    QTest::newRow("time zone")
        << QDateTime(QDate(2019, 05, 21), QTime(12, 0), QTimeZone("Europe/Helsinki"));

    QTest::newRow("all day, local time")
        << QDateTime(QDate(2019, 05, 21), QTime(), Qt::LocalTime);
    QTest::newRow("all day, UTC time")
        << QDateTime(QDate(2019, 05, 21), QTime(), Qt::UTC);
    QTest::newRow("all day, time zone")
        << QDateTime(QDate(2019, 05, 21), QTime(), QTimeZone("Europe/Helsinki"));
}

void tst_storage::tst_dissociateSingleOccurrence()
{
    QFETCH(QDateTime, dateTime);

    KCalCore::Event::Ptr event = KCalCore::Event::Ptr(new KCalCore::Event);
    if (dateTime.timeSpec() == Qt::LocalTime) {
        event->setDtStart(KDateTime(dateTime.date(), dateTime.time(), KDateTime::LocalZone));
    } else if (dateTime.timeSpec() == Qt::UTC) {
        event->setDtStart(KDateTime(dateTime.date(), dateTime.time(), KDateTime::UTC));
    } else {
        event->setDtStart(KDateTime(dateTime.date(), dateTime.time(), KSystemTimeZones::zone(dateTime.timeZone().id())));
    }
    if (dateTime.time().msecsSinceStartOfDay()) {
        event->setDtEnd(event->dtStart().addSecs(3600));
        event->setSummary("Reccurring event");
    } else {
        event->setAllDay(true);
        event->setSummary("Reccurring event all day");
    }
    event->setCreated(KDateTime::currentUtcDateTime().addDays(-1));

    KCalCore::Recurrence *recurrence = event->recurrence();
    recurrence->setDaily(1);
    recurrence->setStartDateTime(event->dtStart());
    QVERIFY(event->recurs());

    QDateTime createdDate = event->created().dateTime();
    KDateTime recId = event->dtStart().addDays(1);
    KCalCore::Incidence::Ptr occurrence = m_calendar->dissociateSingleOccurrence(event, recId, recId.timeSpec());
    QVERIFY(occurrence);
    QVERIFY(occurrence->hasRecurrenceId());
    QCOMPARE(occurrence->recurrenceId(), recId);
    QCOMPARE(recurrence->exDateTimes().length(), 1);
    QCOMPARE(recurrence->exDateTimes()[0], recId);
    QCOMPARE(event->created().dateTime(), createdDate);
    QVERIFY(occurrence->created().secsTo(KDateTime::currentUtcDateTime()) < 2);

    m_calendar->addEvent(event, NotebookId);
    m_calendar->addEvent(occurrence.staticCast<KCalCore::Event>(), NotebookId);
    m_storage->save();
    QString uid = event->uid();
    reloadDb();

    KCalCore::Event::Ptr fetchEvent = m_calendar->event(event->uid());
    QVERIFY(fetchEvent);
    QVERIFY(fetchEvent->recurs());
    KCalCore::Recurrence *fetchRecurrence = event->recurrence();
    QCOMPARE(fetchRecurrence->exDateTimes().length(), 1);
    QCOMPARE(fetchRecurrence->exDateTimes()[0], recId);

    KCalCore::Incidence::List occurences = m_calendar->instances(event);
    QCOMPARE(occurences.length(), 1);
    QCOMPARE(occurences[0]->recurrenceId().dateTime(), recId.dateTime());

    KCalCore::Event::Ptr fetchOccurrence = m_calendar->event(event->uid(), recId);
    QVERIFY(fetchOccurrence);
    QVERIFY(fetchOccurrence->hasRecurrenceId());
    QCOMPARE(fetchOccurrence->recurrenceId(), recId);
}

// Accessor check for the deleted incidences.
void tst_storage::tst_deleted()
{
    mKCal::Notebook::Ptr notebook =
        mKCal::Notebook::Ptr(new mKCal::Notebook("123456789-deletion",
                                                 "test notebook",
                                                 QLatin1String(""),
                                                 "#001122",
                                                 false, // Not shared.
                                                 true, // Is master.
                                                 false, // Not synced to Ovi.
                                                 false, // Writable.
                                                 true, // Visible.
                                                 QLatin1String(""),
                                                 QLatin1String(""),
                                                 0));
    m_storage->addNotebook(notebook);

    KCalCore::Event::Ptr event = KCalCore::Event::Ptr(new KCalCore::Event);
    event->setDtStart(KDateTime::currentUtcDateTime());
    event->setSummary("Deleted event");
    event->setCreated(KDateTime::currentUtcDateTime().addSecs(-3));

    QVERIFY(m_calendar->addEvent(event, "123456789-deletion"));
    m_storage->save();
    reloadDb();
    m_storage->loadNotebookIncidences("123456789-deletion");

    KCalCore::Event::Ptr fetchEvent = m_calendar->event(event->uid());
    QVERIFY(fetchEvent);

    QVERIFY(m_calendar->deleteIncidence(fetchEvent));
    QVERIFY(!m_calendar->event(fetchEvent->uid()));
    QVERIFY(m_calendar->deletedEvent(fetchEvent->uid()));

    m_storage->save();
    reloadDb();
    m_storage->loadNotebookIncidences("123456789-deletion");

    KCalCore::Incidence::List deleted;
    QVERIFY(m_storage->deletedIncidences(&deleted, KDateTime::currentUtcDateTime().addSecs(-2), "123456789-deletion"));
    QCOMPARE(deleted.length(), 1);
    QCOMPARE(deleted[0]->uid(), event->uid());
}

// Accessor check for modified incidences.
void tst_storage::tst_modified()
{
    mKCal::Notebook::Ptr notebook =
        mKCal::Notebook::Ptr(new mKCal::Notebook("123456789-modified",
                                                 "test notebook",
                                                 QLatin1String(""),
                                                 "#001122",
                                                 false, // Not shared.
                                                 true, // Is master.
                                                 false, // Not synced to Ovi.
                                                 false, // Writable.
                                                 true, // Visible.
                                                 QLatin1String(""),
                                                 QLatin1String(""),
                                                 0));
    m_storage->addNotebook(notebook);

    KCalCore::Event::Ptr event = KCalCore::Event::Ptr(new KCalCore::Event);
    event->setDtStart(KDateTime::currentUtcDateTime());
    event->setSummary("Base event");
    event->setCreated(KDateTime::currentUtcDateTime().addSecs(-3));

    QVERIFY(m_calendar->addEvent(event, "123456789-modified"));
    m_storage->save();
    reloadDb();
    m_storage->loadNotebookIncidences("123456789-modified");

    KCalCore::Event::Ptr fetchEvent = m_calendar->event(event->uid());
    QVERIFY(fetchEvent);

    fetchEvent->setSummary("Modified event");

    m_storage->save();
    reloadDb();
    m_storage->loadNotebookIncidences("123456789-modified");

    KCalCore::Incidence::List modified;
    QVERIFY(m_storage->modifiedIncidences(&modified, KDateTime::currentUtcDateTime().addSecs(-2), "123456789-modified"));
    QCOMPARE(modified.length(), 1);
    QCOMPARE(modified[0]->uid(), event->uid());
}

// Accessor check for added incidences, including added incidence from
// dissociation of a recurring event.
void tst_storage::tst_inserted()
{
    mKCal::Notebook::Ptr notebook =
        mKCal::Notebook::Ptr(new mKCal::Notebook("123456789-inserted",
                                                 "test notebook",
                                                 QLatin1String(""),
                                                 "#001122",
                                                 false, // Not shared.
                                                 true, // Is master.
                                                 false, // Not synced to Ovi.
                                                 false, // Writable.
                                                 true, // Visible.
                                                 QLatin1String(""),
                                                 QLatin1String(""),
                                                 0));
    m_storage->addNotebook(notebook);

    KCalCore::Event::Ptr event = KCalCore::Event::Ptr(new KCalCore::Event);
    event->setDtStart(KDateTime::currentUtcDateTime());
    event->setSummary("Inserted event");
    event->setCreated(KDateTime::currentUtcDateTime().addSecs(-10));

    KCalCore::Recurrence *recurrence = event->recurrence();
    recurrence->setDaily(1);
    recurrence->setStartDateTime(event->dtStart());
    QVERIFY(event->recurs());

    QVERIFY(m_calendar->addEvent(event, "123456789-inserted"));
    m_storage->save();
    reloadDb();
    m_storage->loadNotebookIncidences("123456789-inserted");

    KCalCore::Incidence::List inserted;
    QVERIFY(m_storage->insertedIncidences(&inserted, KDateTime::currentUtcDateTime().addSecs(-12), "123456789-inserted"));
    QCOMPARE(inserted.length(), 1);
    QCOMPARE(inserted[0]->uid(), event->uid());

    KDateTime recId = event->dtStart().addDays(1);
    KCalCore::Incidence::Ptr occurrence = m_calendar->dissociateSingleOccurrence(event, recId, recId.timeSpec());
    QVERIFY(occurrence);

    QVERIFY(m_calendar->addEvent(occurrence.staticCast<KCalCore::Event>(), "123456789-inserted"));
    m_storage->save();
    reloadDb();
    m_storage->loadNotebookIncidences("123456789-inserted");

    inserted.clear();
    QVERIFY(m_storage->insertedIncidences(&inserted, KDateTime::currentUtcDateTime().addSecs(-5), "123456789-inserted"));
    QCOMPARE(inserted.length(), 1);
    QCOMPARE(inserted[0]->uid(), event->uid());
    QCOMPARE(inserted[0]->recurrenceId().dateTime(), recId.dateTime());

    KCalCore::Incidence::List modified;
    QVERIFY(m_storage->modifiedIncidences(&modified, KDateTime::currentUtcDateTime().addSecs(-5), "123456789-inserted"));
    QCOMPARE(modified.length(), 1);
    QCOMPARE(modified[0]->uid(), event->uid());
    QCOMPARE(modified[0]->recurrenceId().dateTime(), QDateTime());
}

// Test various way of describing all day events in iCal format.
void tst_storage::tst_icalAllDay_data()
{
    QTest::addColumn<QString>("uid");
    QTest::addColumn<QString>("vEvent");
    QTest::addColumn<bool>("allDay");

    QTest::newRow("local time")
        << QStringLiteral("14B902BC-8D24-4A97-8541-63DF7FD41A70")
        << QStringLiteral("BEGIN:VEVENT\n"
                          "DTSTART:20190607T000000\n"
                          "DTEND:20190607T000000\n"
                          "UID:14B902BC-8D24-4A97-8541-63DF7FD41A70\n"
                          "SUMMARY:Test03\n"
                          "END:VEVENT")
        << true;
    QTest::newRow("UTC")
        << QStringLiteral("14B902BC-8D24-4A97-8541-63DF7FD41A71")
        << QStringLiteral("BEGIN:VEVENT\n"
                          "DTSTART:20190607T000000Z\n"
                          "DTEND:20190607T000000Z\n"
                          "UID:14B902BC-8D24-4A97-8541-63DF7FD41A71\n"
                          "SUMMARY:Test03\n"
                          "END:VEVENT")
        << false;
    QString zid(KSystemTimeZones::local().name());
    QTest::newRow("system time zone")
        << QStringLiteral("14B902BC-8D24-4A97-8541-63DF7FD41A72")
        << QStringLiteral("BEGIN:VEVENT\n"
                          "DTSTART;TZID=%1:20190607T000000\n"
                          "DTEND;TZID=%1:20190607T000000\n"
                          "UID:14B902BC-8D24-4A97-8541-63DF7FD41A72\n"
                          "SUMMARY:Test03\n"
                          "END:VEVENT").arg(zid)
        << false;
    QTest::newRow("floating date")
        << QStringLiteral("14B902BC-8D24-4A97-8541-63DF7FD41A73")
        << QStringLiteral("BEGIN:VEVENT\n"
                          "DTSTART:20190607\n"
                          "UID:14B902BC-8D24-4A97-8541-63DF7FD41A73\n"
                          "SUMMARY:Test03\n"
                          "END:VEVENT")
        << true;
}

void tst_storage::tst_icalAllDay()
{
    QFETCH(QString, uid);
    QFETCH(QString, vEvent);
    QFETCH(bool, allDay);

    const QString icsData =
        QStringLiteral("BEGIN:VCALENDAR\n"
                       "PRODID:-//NemoMobile.org/Nemo//NONSGML v1.0//EN\n"
                       "VERSION:2.0\n") + vEvent +
        QStringLiteral("\nEND:VCALENDAR");
    KCalCore::ICalFormat format;
    QVERIFY(format.fromString(m_calendar, icsData));
    KCalCore::Event::Ptr event = m_calendar->event(uid);
    QVERIFY(event);

    m_storage->save();
    reloadDb();

    KCalCore::Event::Ptr fetchEvent = m_calendar->event(event->uid());
    QVERIFY(fetchEvent);
    QCOMPARE(fetchEvent->allDay(), allDay);
    QCOMPARE(event->dtStart(), fetchEvent->dtStart());
    QCOMPARE(event->dtEnd(), fetchEvent->dtEnd());
}

void tst_storage::tst_deleteAllEvents()
{
    ExtendedCalendar::Ptr cal = ExtendedCalendar::Ptr(new ExtendedCalendar(KDateTime::Spec::LocalZone()));
    QVERIFY(cal->addNotebook(QStringLiteral("notebook"), true));
    QVERIFY(cal->setDefaultNotebook(QStringLiteral("notebook")));

    KCalCore::Event::Ptr ev = KCalCore::Event::Ptr(new KCalCore::Event);
    ev->setLastModified(KDateTime::currentUtcDateTime().addSecs(-42));
    ev->setHasGeo(true);
    ev->setGeoLatitude(42.);
    ev->setGeoLongitude(42.);
    ev->setDtStart(KDateTime(QDate(2019, 10, 10)));
    KCalCore::Attendee::Ptr bob = KCalCore::Attendee::Ptr(new KCalCore::Attendee(QStringLiteral("Bob"), QStringLiteral("bob@example.org")));
    ev->addAttendee(bob);

    QVERIFY(cal->addIncidence(ev));
    QCOMPARE(cal->incidences().count(), 1);
    QCOMPARE(cal->geoIncidences().count(), 1);
    QCOMPARE(cal->attendees().count(), 1);
    QCOMPARE(cal->rawEventsForDate(ev->dtStart().date()).count(), 1);

    cal->deleteAllEvents();
    QVERIFY(cal->incidences().isEmpty());
    QVERIFY(cal->geoIncidences().isEmpty());
    QVERIFY(cal->attendees().isEmpty());
    QVERIFY(cal->rawEventsForDate(ev->dtStart().date()).isEmpty());
}

void tst_storage::tst_calendarProperties()
{
    Notebook::Ptr notebook = Notebook::Ptr(new Notebook(QStringLiteral("Notebook"), QString()));

    QCOMPARE(notebook->customPropertyKeys().count(), 0);
    const QByteArray propKey("a key");
    const QString propValue = QStringLiteral("a value");
    notebook->setCustomProperty(propKey, propValue);
    QCOMPARE(notebook->customPropertyKeys().count(), 1);
    QCOMPARE(notebook->customProperty(propKey), propValue);

    QVERIFY(m_storage->addNotebook(notebook));
    QString uid = notebook->uid();

    reloadDb();
    notebook = m_storage->notebook(uid);
    QVERIFY(notebook);
    QCOMPARE(notebook->customPropertyKeys().count(), 1);
    QCOMPARE(notebook->customProperty(propKey), propValue);

    const QByteArray propKey2("a second key");
    const QString propValue2 = QStringLiteral("another value");
    notebook->setCustomProperty(propKey2, propValue2);
    QCOMPARE(notebook->customPropertyKeys().count(), 2);
    QCOMPARE(notebook->customProperty(propKey2), propValue2);

    QVERIFY(m_storage->updateNotebook(notebook));

    reloadDb();
    notebook = m_storage->notebook(uid);
    QVERIFY(notebook);
    QCOMPARE(notebook->customPropertyKeys().count(), 2);
    QCOMPARE(notebook->customProperty(propKey), propValue);
    QCOMPARE(notebook->customProperty(propKey2), propValue2);

    notebook->setCustomProperty(propKey2, QString());
    QCOMPARE(notebook->customPropertyKeys().count(), 1);
    QCOMPARE(notebook->customProperty(propKey), propValue);
    QCOMPARE(notebook->customProperty(propKey2), QString());
    QString defaultValue = QStringLiteral("default value");
    QCOMPARE(notebook->customProperty(propKey2, defaultValue), defaultValue);

    QVERIFY(m_storage->updateNotebook(notebook));

    reloadDb();
    notebook = m_storage->notebook(uid);
    QVERIFY(notebook);
    QCOMPARE(notebook->customPropertyKeys().count(), 1);
    QCOMPARE(notebook->customProperty(propKey), propValue);
    QCOMPARE(notebook->customProperty(propKey2), QString());

    m_storage->deleteNotebook(notebook);

    // Need to check by hand that property entries have been deleted.
    int rv;
    sqlite3 *database;
    rv = sqlite3_open(m_storage.staticCast<SqliteStorage>()->databaseName().toUtf8(), &database);
    QCOMPARE(rv, 0);
    const char *query = SELECT_CALENDARPROPERTIES_BY_ID;
    int qsize = sizeof(SELECT_CALENDARPROPERTIES_BY_ID);
    sqlite3_stmt *stmt = NULL;
#undef sqlite3_prepare_v2
    rv = sqlite3_prepare_v2(database, query, qsize, &stmt, NULL);
    QCOMPARE(rv, 0);
    const QByteArray id(uid.toUtf8());
#undef sqlite3_bind_text
    rv = sqlite3_bind_text(stmt, 1, id.constData(), id.length(), SQLITE_STATIC);
    QCOMPARE(rv, 0);
#undef sqlite3_step
    rv = sqlite3_step(stmt);
    QCOMPARE(rv, SQLITE_DONE);
    sqlite3_close(database);
}


void tst_storage::openDb(bool clear)
{
    m_calendar = ExtendedCalendar::Ptr(new ExtendedCalendar(KDateTime::Spec::LocalZone()));
    m_storage = m_calendar->defaultStorage(m_calendar);
    m_storage->open();

    mKCal::Notebook::Ptr notebook = m_storage->notebook(NotebookId);

    if (notebook.data() && clear) {
        m_storage->deleteNotebook(notebook);
        notebook.clear();
    }

    if (notebook.isNull()) {
        notebook = mKCal::Notebook::Ptr(new mKCal::Notebook(NotebookId,
                                                            "test notebook",
                                                            QLatin1String(""),
                                                            "#001122",
                                                            false, // Not shared.
                                                            true, // Is master.
                                                            false, // Not synced to Ovi.
                                                            false, // Writable.
                                                            true, // Visible.
                                                            QLatin1String(""),
                                                            QLatin1String(""),
                                                            0));
        m_storage->addNotebook(notebook);
        m_storage->setDefaultNotebook(notebook);
    }

    m_storage->loadNotebookIncidences(NotebookId);
}

void tst_storage::reloadDb()
{
    m_storage.clear();
    m_calendar.clear();
    openDb();
}

QTEST_GUILESS_MAIN(tst_storage)
