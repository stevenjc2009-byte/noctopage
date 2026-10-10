#include "test.h"
#include "lib/db.h"
#include "lib/db_priv.h"
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

static void add(uint64_t id,const char *path)
{
	db_book_t b={0}; b.id=id; b.kind=DB_KIND_ARCHIVE;
	strcpy(b.path,path); strcpy(b.title,path); b.size=100;
	CHECK_INT(db_book_upsert(&b),1);
}

int main(void)
{
	mkdir("build-host-db",0777);
	const char *file="build-host-db/features.db";
	unlink(file);
	/* Upgrade a real version-1 library, preserving title and position. */
	sqlite3 *raw=NULL;
	CHECK_INT(sqlite3_open(file,&raw),SQLITE_OK);
	CHECK_INT(sqlite3_exec(raw,db_migrations[0],NULL,NULL,NULL),SQLITE_OK);
	CHECK_INT(sqlite3_exec(raw,"PRAGMA user_version=1; INSERT INTO books(id,path,title,page,progress,last_read) VALUES(99,'ux0:books/old.cbz','My custom title',7,0.4,12)",NULL,NULL,NULL),SQLITE_OK);
	sqlite3_close(raw);
	CHECK_INT(db_open(file),0);
	db_book_t b,out;
	CHECK_INT(db_book_get(99,&b),1); CHECK_STR(b.title,"My custom title"); CHECK_INT(b.page,7);
	CHECK_INT(b.favorite,0); CHECK_U64(b.reading_ms,0);
	CHECK_INT(db_scan_begin(),0);
	add(1,"ux0:books/Tokyo Ghoul/Volume 1.cbz");
	add(2,"ux0:books/Tokyo Ghoul/Volume 2.cbz");
	add(10,"ux0:books/Tokyo Ghoul/Volume 10.cbz");
	add(20,"ux0:books/Other/Volume 3.cbz");
	add(21,"ux0:books/Tokyo Ghoul/nested/Volume 3.cbz");
	CHECK_INT(db_scan_end(),0);
	CHECK_INT(db_next_volume(1,&out),1); CHECK_U64(out.id,2);
	CHECK_INT(db_next_volume(2,&out),1); CHECK_U64(out.id,10);
	CHECK_INT(db_next_volume(10,&out),0);
	CHECK_INT(db_next_volume(99,&out),0);
	CHECK_INT(db_set_title(2,"Custom cover title","Sui Ishida"),1);
	CHECK_INT(db_set_favorite(2,1),1);
	CHECK_INT(db_reading_add(2,60000,3),1);
	CHECK_INT(db_reading_add(2,15000,2),1);
	CHECK_INT(db_reading_add(2,3600001,0),-1);
	CHECK_INT(db_set_progress(2,4,10,"page4",0.4),1);
	CHECK_INT(db_book_setting_set(2,"cover_path","ux0:data/noctopage/covers/custom.bin"),0);
	CHECK_INT(db_book_get(2,&b),1);
	CHECK_INT(b.favorite,1); CHECK_U64(b.reading_ms,75000); CHECK_INT(b.pages_read,5);
	CHECK_INT(db_set_finished(1,1),1);
	db_reading_stats_t stats;
	CHECK_INT(db_reading_stats(&stats),1); CHECK_U64(stats.milliseconds,75000);
	CHECK_INT(stats.pages,5); CHECK_INT(stats.finished,1); CHECK_INT(stats.reading,1); CHECK_INT(stats.total,5);
	db_close(); CHECK_INT(db_open(file),0);
	CHECK_INT(db_book_get(2,&b),1); CHECK_STR(b.title,"Custom cover title"); CHECK_INT(b.favorite,1); CHECK_U64(b.reading_ms,75000);
	char custom[128]; CHECK_INT(db_book_setting_get(2,"cover_path",custom,sizeof custom),1);
	CHECK_STR(custom,"ux0:data/noctopage/covers/custom.bin");
	CHECK_INT(db_mark_unread(2),1); CHECK_INT(db_book_get(2,&b),1);
	CHECK_INT(b.page,0); CHECK_INT(b.last_read,0); CHECK_INT(b.finished,0); CHECK_U64(b.reading_ms,75000);
	/* Missing volumes are skipped, and old game-icon rows vanish from My Books. */
	CHECK_INT(db_scan_begin(),0); CHECK_INT(db_scan_lookup("ux0:books/Tokyo Ghoul/Volume 1.cbz",100,0),1);
	CHECK_INT(db_scan_lookup("ux0:books/Tokyo Ghoul/Volume 10.cbz",100,0),1);
	CHECK_INT(db_scan_end(),0); CHECK_INT(db_next_volume(1,&out),1); CHECK_U64(out.id,10);
	/* Private metadata survives rescans/reopen/backups and never leaks through
	 * the public continue/history or automatic next-volume path. */
	CHECK_INT(db_history_remove(99),1);
	CHECK_INT(db_set_finished(1,0),1);
	CHECK_INT(db_set_private(10,1),0);
	CHECK_INT(db_books_count_section(0),1); CHECK_INT(db_books_count_section(1),1);
	db_book_t list[8];
	for (int sort=0; sort<3; sort++) {
		CHECK_INT(db_books_list_section(sort,0,list,8),1); CHECK_U64(list[0].id,1);
		CHECK_INT(db_books_list_section(sort,1,list,8),1); CHECK_U64(list[0].id,10);
	}
	CHECK_INT(db_next_volume(1,&out),0);
	CHECK_INT(db_continue_series("Tokyo Ghoul",&out),1); CHECK_U64(out.id,1);
	CHECK_INT(db_continue_series_section("Tokyo Ghoul",1,&out),1); CHECK_U64(out.id,10);
	CHECK_INT(db_set_progress(10,1,20,"p:1",0.1),1);
	CHECK_INT(db_continue_reading(&out),0);
	CHECK_INT(db_continue_reading_section(1,&out),1); CHECK_U64(out.id,10);
	CHECK_INT(db_history_list(list,8),0);
	CHECK_INT(db_set_series(10,"Custom series"),0);
	CHECK_INT(db_book_get(10,&b),1); CHECK_STR(b.series,"Custom series");
	CHECK_INT(db_scan_begin(),0);
	CHECK_INT(db_scan_lookup("ux0:books/Tokyo Ghoul/Volume 1.cbz",100,0),1);
	CHECK_INT(db_scan_lookup("ux0:books/Tokyo Ghoul/Volume 10.cbz",100,0),1);
	CHECK_INT(db_scan_end(),0);
	db_close(); CHECK_INT(db_open(file),0);
	CHECK_INT(db_book_get(10,&b),1); CHECK_INT(b.private_book,1); CHECK_STR(b.series,"Custom series");
	CHECK_INT(db_continue_series_section("Custom series",1,&out),1); CHECK_U64(out.id,10);
	const char *snapshot="build-host-db/shelf-backup.db"; unlink(snapshot);
	CHECK_INT(db_library_snapshot(snapshot),0);
	CHECK_INT(db_set_private(10,0),0); CHECK_INT(db_set_series(10,""),0);
	CHECK_INT(db_next_volume(1,&out),1); CHECK_U64(out.id,10);
	CHECK_INT(db_library_restore(snapshot),0);
	CHECK_INT(db_book_get(10,&b),1); CHECK_INT(b.private_book,1); CHECK_STR(b.series,"Custom series");
	CHECK_INT(b.page,1); CHECK_INT(b.page_count,20);
	CHECK_INT(db_history_list(list,8),0);
	/* A backup from before this feature remains readable and clears newer flags. */
	CHECK_INT(sqlite3_open(snapshot,&raw),SQLITE_OK);
	CHECK_INT(sqlite3_exec(raw,"DELETE FROM book_settings WHERE key IN ('private','series')",NULL,NULL,NULL),SQLITE_OK);
	sqlite3_close(raw);
	CHECK_INT(db_library_restore(snapshot),0);
	CHECK_INT(db_book_get(10,&b),1); CHECK_INT(b.private_book,0); CHECK_STR(b.series,"");
	CHECK_INT(db_history_list(list,8),1); CHECK_U64(list[0].id,10);
	/* Natural order and next/continue agree for loose and manually assigned books. */
	add(30,"ux0:books/One Piece v2.cbz"); add(31,"ux0:books/One Piece v10.cbz");
	CHECK_INT(db_next_volume(30,&out),1); CHECK_U64(out.id,31);
	CHECK_INT(db_continue_series("One Piece",&out),1); CHECK_U64(out.id,30);
	CHECK_INT(db_set_series(31,"Other series"),0); CHECK_INT(db_next_volume(30,&out),0);
	CHECK_INT(db_set_series(30,"Other series"),0); CHECK_INT(db_next_volume(30,&out),1);
	CHECK_INT(db_set_private(30,1),0); CHECK_INT(db_set_private(31,1),0);
	CHECK_INT(db_next_volume(30,&out),1); CHECK_U64(out.id,31);
	CHECK_INT(db_continue_series("Other series",&out),0);
	CHECK_INT(db_continue_series_section("Other series",1,&out),1); CHECK_U64(out.id,30);
	CHECK_INT(db_set_private(31,0),0); CHECK_INT(db_next_volume(30,&out),0);
	CHECK_INT(db_books_list_section(0,0,list,0),0);
	CHECK_INT(db_set_series(1,NULL),-1);
	CHECK_INT(db_set_series(1,"  Tokyo Ghoul \t"),0);
	CHECK_INT(db_book_get(1,&b),1); CHECK_STR(b.series,"Tokyo Ghoul");
	CHECK_INT(db_set_series(1," \t "),0);
	CHECK_INT(db_book_get(1,&b),1); CHECK_STR(b.series,"");
	/* The navigation query must use the same mixed-marker numeric order. */
	add(50,"ux0:books/Mixed/Vol 10.cbz");add(51,"ux0:books/Mixed/Volume 2.cbz");
	CHECK_INT(db_next_volume(51,&out),1); CHECK_U64(out.id,50);
	CHECK_INT(db_continue_series("Mixed",&out),1);CHECK_U64(out.id,51);
	/* A failed book DELETE must leave settings/bookmarks intact too. */
	add(40,"ux0:books/atomic.cbz");CHECK_INT(db_set_private(40,1),0);
	db_bookmark_t mark={0};mark.book_id=40;strcpy(mark.label,"Keep me");int64_t mark_id;
	CHECK_INT(db_bookmark_add(&mark,&mark_id),0);
	CHECK_INT(sqlite3_open(file,&raw),SQLITE_OK);
	CHECK_INT(sqlite3_exec(raw,"CREATE TRIGGER block_delete BEFORE DELETE ON books WHEN OLD.id=40 BEGIN SELECT RAISE(ABORT,'injected delete failure'); END;",NULL,NULL,NULL),SQLITE_OK);
	sqlite3_close(raw);
	CHECK_INT(db_book_remove(40),-1);
	CHECK(strstr(db_last_error(),"injected delete failure")!=NULL);
	CHECK_INT(db_book_get(40,&b),1);CHECK_INT(b.private_book,1);
	db_bookmark_t marks[2];CHECK_INT(db_bookmark_list(40,marks,2),1);CHECK_STR(marks[0].label,"Keep me");
	CHECK_INT(sqlite3_open(file,&raw),SQLITE_OK);
	CHECK_INT(sqlite3_exec(raw,"DROP TRIGGER block_delete",NULL,NULL,NULL),SQLITE_OK);sqlite3_close(raw);
	CHECK_INT(db_book_remove(40),1);CHECK_INT(db_book_get(40,&b),0);
	char setting[8];CHECK_INT(db_book_setting_get(40,"private",setting,sizeof setting),0);
	CHECK_INT(db_bookmark_list(40,marks,2),0);
	CHECK_INT(db_book_remove(40),0);

 /* Real transaction, collections, inheritance and progress-only backup round trip. */
 uint64_t ids[2]={1,2}; CHECK_INT(db_set_private(1,0),0); CHECK_INT(db_set_private(2,0),0);
 CHECK_INT(db_bulk_private(ids,2,1),0); CHECK_INT(db_book_get(1,&b),1); CHECK_INT(b.private_book,1); CHECK_INT(db_book_get(2,&b),1); CHECK_INT(b.private_book,1);
 CHECK_INT(db_undo_move(),1); CHECK_INT(db_book_get(1,&b),1); CHECK_INT(b.private_book,0); CHECK_INT(db_book_get(2,&b),1); CHECK_INT(b.private_book,0);
 int64_t collection; CHECK_INT(db_collection_create("My next reads",&collection),0); CHECK(db_collection_add(collection,1)>=0); CHECK_INT(db_collection_contains(collection,1),1); CHECK_INT(db_collection_contains(collection,2),0); CHECK(db_collection_remove(collection,1)>=0); CHECK_INT(db_collection_contains(collection,1),0);
 CHECK_INT(db_series_setting_set("Tokyo Ghoul","mode","1"),0); char value[64]; CHECK_INT(db_effective_setting(1,"mode",value,sizeof value),1); CHECK_STR(value,"1"); CHECK_INT(db_book_setting_set(1,"mode","0"),0); CHECK_INT(db_effective_setting(1,"mode",value,sizeof value),1); CHECK_STR(value,"0");
 CHECK_INT(db_book_setting_set(2,"mode","1"),0); CHECK_INT(db_apply_series_settings(1),0); CHECK_INT(db_effective_setting(2,"mode",value,sizeof value),1); CHECK_STR(value,"0"); CHECK_INT(db_book_setting_set(2,"mode","1"),0); CHECK_INT(db_effective_setting(2,"mode",value,sizeof value),1); CHECK_STR(value,"1"); CHECK_INT(db_series_setting_get("Tokyo Ghoul","mode",value,sizeof value),1); CHECK_STR(value,"0");
 const char *reading="build-host-db/reading-only.db"; unlink(reading);
 CHECK_INT(db_set_progress(1,17,100,"page18",0.17),1); CHECK_INT(db_reading_snapshot(reading),0);
 CHECK_INT(sqlite3_open(reading,&raw),SQLITE_OK); sqlite3_stmt *q=NULL;
 CHECK_INT(sqlite3_prepare_v2(raw,"SELECT count(*) FROM sqlite_master WHERE type='table'",-1,&q,NULL),SQLITE_OK); CHECK_INT(sqlite3_step(q),SQLITE_ROW); CHECK_INT(sqlite3_column_int(q,0),2); sqlite3_finalize(q); sqlite3_close(raw);
 CHECK_INT(db_mark_unread(1),1); CHECK_INT(db_set_title(1,"New title","New author"),1); CHECK_INT(db_set_private(1,1),0);
 CHECK_INT(db_reading_restore(reading),0); CHECK_INT(db_book_get(1,&b),1); CHECK_INT(b.page,17); CHECK_STR(b.location,"page18"); CHECK_STR(b.title,"New title"); CHECK_INT(b.private_book,1);
 CHECK_INT(sqlite3_open(reading,&raw),SQLITE_OK); CHECK_INT(sqlite3_exec(raw,"UPDATE reading_state SET progress=2",NULL,NULL,NULL),SQLITE_OK); sqlite3_close(raw);
 CHECK_INT(db_reading_restore(reading),-1); CHECK_INT(db_book_get(1,&b),1); CHECK_INT(b.page,17); unlink(reading);

 CHECK_INT(db_book_get(2,&b),1); CHECK(db_book_upsert(&b)>=0); CHECK_INT(db_set_private(2,1),0);
 unsigned char owned[1001]; CHECK_INT(db_series_volume_numbers("Tokyo Ghoul",owned,sizeof owned),0); CHECK(owned[1]); CHECK(owned[2]); CHECK(owned[10]); CHECK(!owned[7]);
 /* Inject failure into the second move: first move must roll back with it. */
 CHECK_INT(db_set_private(1,0),0); CHECK_INT(db_set_private(2,0),0);
 CHECK_INT(sqlite3_open(file,&raw),SQLITE_OK); CHECK_INT(sqlite3_exec(raw,"CREATE TRIGGER block_move BEFORE UPDATE OF value ON book_settings WHEN OLD.book_id=2 AND OLD.key='private' BEGIN SELECT RAISE(ABORT,'injected move failure'); END",NULL,NULL,NULL),SQLITE_OK); sqlite3_close(raw);
 CHECK_INT(db_bulk_private(ids,2,1),-1); CHECK_INT(db_book_get(1,&b),1); CHECK_INT(b.private_book,0);
 CHECK_INT(sqlite3_open(file,&raw),SQLITE_OK); CHECK_INT(sqlite3_exec(raw,"DROP TRIGGER block_move",NULL,NULL,NULL),SQLITE_OK); sqlite3_close(raw);
 unlink(snapshot);
 db_close(); unlink(file);
	return TEST_DONE("features");
}
