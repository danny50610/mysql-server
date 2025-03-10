#
# Bug #49931 and Bug #49932
# This test verifies if the slave I/O thread and slave SQL thread
# will check the bigger one of the values of 'max_allowed_packet' 
# and 'binlog-row-event-max-size' when reading log event from
# binlog, and the added 'binlog-row-event-max-size' mysqlbinlog 
# option works fine.
# 

source include/master-slave.inc;
source include/have_binlog_format_row.inc;

SET SESSION sql_log_bin= 0;
# Suppression of valgrind warning:
CALL mtr.add_suppression("==[0-9]*== Warning: set address range perms: large range");
SET SESSION sql_log_bin= 1;

--let $binlog_filename= query_get_value(SHOW BINARY LOG STATUS, File, 1)
--let $MYSQLD_DATADIR= `select @@datadir`

let $binlog_start= query_get_value("SHOW BINARY LOG STATUS", Position, 1);
CREATE TABLE t1 (a int not null auto_increment, data1 LONGBLOB,
                 data2 LONGBLOB, PRIMARY KEY(a));

let $start_pos= query_get_value("SHOW BINARY LOG STATUS", Position, 1);
INSERT INTO t1 (data1, data2) VALUES (repeat('a',1000000), repeat('a', 1000000));
let $end_pos= query_get_value("SHOW BINARY LOG STATUS", Position, 1);
#FLUSH LOGS;
DELETE FROM t1 WHERE a = 1;
--echo # On master, test the inserted data is deleted
SELECT LENGTH(data1), LENGTH(data2) FROM t1 WHERE a = 1;

let $old_max_allowed_packet= `SELECT @@global.max_allowed_packet`;
SET @@global.max_allowed_packet=4194304;
FLUSH LOGS;
--let $prefix=`SELECT UUID()`
--let $binlog_uuid_filename= $MYSQLTEST_VARDIR/tmp/$prefix-bin.log
--copy_file $MYSQLD_DATADIR/$binlog_filename $binlog_uuid_filename
--source include/rpl_reset.inc

--echo # On master, test the added 'binlog-row-event-max-size' mysqlbinlog option
--echo # works fine and the data is inserted by executing the dumped ROW event
--exec $MYSQL_BINLOG --binlog-row-event-max-size=2097152  --start-position=$start_pos --stop-position=$end_pos $binlog_uuid_filename | $MYSQL test 2>&1
SELECT LENGTH(data1), LENGTH(data2) FROM t1 WHERE a = 1;
eval set @@global.max_allowed_packet= $old_max_allowed_packet;

INSERT INTO t1 (data1, data2) VALUES (repeat('a',1048576), repeat('a',1048576));

--source suite/rpl/include/rpl_row_event_max_size_show_binlog.inc

--source include/sync_slave_sql_with_master.inc
--echo # On slave, test the row event data is replicated when the value of
--echo # the bigger one of max_allowed_packet and binlog_row_event_max_size
--echo # is larger than the required size.
SELECT LENGTH(data1), LENGTH(data2) FROM t1 WHERE a = 1;

--echo # On slave, test the row event data is replicated when the value of
--echo # the bigger one of max_allowed_packet and binlog_row_event_max_size
--echo # is equal to the required size.
SELECT LENGTH(data1), LENGTH(data2) FROM t1 WHERE a = 2;

connection master;
DROP TABLE t1;

--source include/sync_slave_sql_with_master.inc
--source include/rpl_reset.inc

# Bug #18233370

--echo # Test it will cause ER_SOURCE_FATAL_ERROR_READING_BINLOG when the value
--echo # of the bigger one of max_allowed_packet and binlog_row_event_max_size
--echo # is lower than the the required size.
connection master;

CREATE TABLE t1 (a int not null auto_increment, data1 LONGBLOB,
                 data2 LONGBLOB, data3 LONGBLOB, PRIMARY KEY(a)) ROW_FORMAT=DYNAMIC;

--source include/sync_slave_sql_with_master.inc
--connection master

INSERT INTO t1 (data1, data2, data3) VALUES (repeat('a', @@global.max_allowed_packet), repeat('a', @@global.max_allowed_packet), repeat('a', @@global.max_allowed_packet));

SELECT LENGTH(data1), LENGTH(data2), LENGTH(data3) FROM t1 WHERE a = 1;

drop table t1;
--source include/sync_slave_sql_with_master.inc

connection slave;
call mtr.add_suppression("Replica I/O: Got fatal error 1236 from source when reading data from binary log: .*");
call mtr.add_suppression("Found invalid event in binary log");
call mtr.add_suppression("The replica coordinator and worker threads are stopped, possibly leaving data in inconsistent state");
# Suppression of valgrind warning:
CALL mtr.add_suppression("==[0-9]*== Warning: set address range perms: large range");

--echo ==== clean up ====

# clear errno
--source include/stop_slave.inc
RESET SLAVE;
RESET BINARY LOGS AND GTIDS;

--remove_file $binlog_uuid_filename
--let $rpl_only_running_threads= 1
--source include/rpl_end.inc
