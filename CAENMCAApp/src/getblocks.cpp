#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <tuple>
#include <map>
#include <mutex>
#include <sys/stat.h>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <errlog.h>

#include "boost/algorithm/string/join.hpp"
#include <boost/algorithm/string.hpp>


// mysql
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/warning.h>
#include <cppconn/metadata.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/resultset_metadata.h>
#include <cppconn/statement.h>
#include "mysql_driver.h"
#include "mysql_connection.h"

#include <epicsExport.h>

#include <getblocks.h>

using namespace std::string_literals; // enable s suffix for std::string literals

static sql::Driver* mysql_driver = NULL;

static std::map<int, std::string> g_status_map;
static std::map<int, std::string> g_severity_map;
static std::mutex g_sev_stat_mutex;

static void create_lookup(std::shared_ptr<sql::Connection> con, std::map<int, std::string>& table, const std::string& table_name, 
	                      const std::string& id_col, const std::string& name_col)
{
    std::unique_ptr<sql::Statement> stmt(con->createStatement());
	std::vector<int> id;
	std::vector<std::string> name;
    std::ostringstream stmt_sql;
	stmt_sql << "SELECT " << id_col << "," << name_col << " FROM " << table_name;
	std::unique_ptr< sql::ResultSet > res{stmt->executeQuery(stmt_sql.str())};
    while(res->next()) {
        id.push_back(res->getInt(1));
        name.push_back(res->getString(2));
    }
	if (id.size() > 0)
	{
		std::lock_guard<std::mutex> _guard(g_sev_stat_mutex);
		table.clear();
		for (int i = 0; i < id.size(); ++i)
		{
			table[id[i]] = name[i];
		}
	}
}

static std::string print_lookup(const std::map<int, std::string>& table, int id, const std::string& error_type)
{
	std::lock_guard<std::mutex> _guard(g_sev_stat_mutex);
	std::map<int, std::string>::const_iterator it;
	if ( (it = table.find(id)) == table.cend() )
	{
		return error_type + "_" + std::to_string(id);
	}
	else
	{
		return error_type + "_" + it->second;
	}
}

struct stat_sev 
{
	int stat;
	int sev;
	stat_sev() : stat(0), sev(0) {}
};

epicsShareExtern void getBlocks()
{
    epicsThreadSleep(30);
	std::string mysqlHost = "localhost";
	if (mysql_driver == NULL)
	{
		mysql_driver = sql::mysql::get_driver_instance();
	}
	std::shared_ptr< sql::Connection > archive_session(mysql_driver->connect(mysqlHost, "report", "$report"));
	// the ORDER BY is to make deletes happen in a consistent primary key order, and so try and avoid deadlocks
	// but it may not be completely right. Additional indexes have also been added to database tables.
	//archive_session->setAutoCommit(0); // we will create transactions ourselves via explicit calls to con->commit()
	archive_session->setSchema("archive");

	//select eng_id from smpl_engine where name='inst_engine' 'block_engine'
	//select grp_id from chan_grp where eng_id= and name='BLOCKS' 'INST'
	//select channel_id,name,descr from channel where grp_id=

	//select grp_id from chan_grp inner join smpl_eng on chan_grp.eng_id=smpl_eng.eng_id and smpl_eng.name='block_engine' and chan_grp.name='BLOCKS';


	std::vector<std::string> channel_name;
	std::vector<double> smpl_time;
	std::vector<std::string> array_channel_name, array_smpl_time;
	std::vector<std::string> str_val;
	std::vector<int> channel_id, array_channel_id, status_id, array_status_id, severity_id, array_severity_id;
	std::vector<unsigned> nanosecs, array_nanosecs, putlog_id;
	std::vector<uint64_t> sample_id, array_sample_id;
	std::map<std::string, stat_sev> chan_stat_sev;
	std::map<std::string, std::string> channel_units_map;
	bool in_retry = false;
	std::vector<std::string> alarm_excludes_vec;

	alarm_excludes_vec.push_back("'Disconnected'");
	alarm_excludes_vec.push_back("'Archive_Off'");
	std::string alarm_excludes = boost::algorithm::join(alarm_excludes_vec, ",");
	std::unique_ptr<sql::Statement>  epics_stmt1(archive_session->createStatement()), epics_stmt2(archive_session->createStatement()), epics_stmt3(archive_session->createStatement()), epics_stmt4(archive_session->createStatement());
	int grp_id; // group id for where SECI like block PVs will go - we need to check this each time as it changhes when new blocks are loaded
//	std::unique_ptr< sql::ResultSet > res1{ epics_stmt1->executeQuery("SELECT grp_id FROM chan_grp INNER JOIN smpl_eng ON chan_grp.eng_id=smpl_eng.eng_id AND smpl_eng.name='block_engine' AND chan_grp.name='BLOCKS'") };
	std::unique_ptr< sql::ResultSet > res1{ epics_stmt1->executeQuery("SELECT grp_id FROM chan_grp INNER JOIN smpl_eng ON chan_grp.eng_id=smpl_eng.eng_id AND smpl_eng.name='inst_engine' AND chan_grp.name='INST'") };
    if (res1->next()) {
	    grp_id = res1->getInt(1);
    }

	// when EPICS archiver is restarted, we can get a stream of NULL values followed bythe  real values, hence "is not null"
	std::ostringstream stmt2_sql;
	stmt2_sql << "SELECT * FROM (SELECT channel.name, channel.channel_id, sample_id, UNIX_TIMESTAMP(smpl_time), nanosecs, severity_id, status_id, COALESCE(num_val, float_val, str_val) AS val "
		<< " FROM channel INNER JOIN sample ON (channel.channel_id=sample.channel_id) "
		<< " AND (grp_id=" << grp_id << ") AND (smpl_time >= '" << "2025-01-01 10:00:05" << "') AND (array_val IS NULL) " // datatype column only set for arrays
		<< " ORDER BY smpl_time, nanosecs) sub WHERE sub.val IS NOT NULL"
		<< (alarm_excludes.size() > 0 ? " AND CAST(sub.val AS CHAR) NOT IN ("s + alarm_excludes + ")" : ""s);
	// limit query
	std::unique_ptr< sql::ResultSet > res2{ epics_stmt2->executeQuery(stmt2_sql.str()) };
	while (res2->next()) {
		channel_name.push_back(res2->getString(1));
		channel_id.push_back(res2->getInt(2));
		sample_id.push_back(res2->getInt(3));
		smpl_time.push_back(res2->getDouble(4));
		nanosecs.push_back(res2->getInt(5));
		severity_id.push_back(res2->getInt(6));
		status_id.push_back(res2->getInt(7));
		str_val.push_back(res2->getString(8));
	}

	for (int i = 0; i < str_val.size(); ++i)
	{
		if (!stricmp(str_val[i].c_str(), "Disconnected") || !stricmp(str_val[i].c_str(), "Archive_Off"))
		{
			str_val[i] = "0"; // we are in alarm so value does not matter, but we do not know value data type so use "0" as valid for both numeric and string
		}
	}
	// the archive engine can dynamically add new status/severity entries, so we need to check table each time
	create_lookup(archive_session, g_status_map, "status", "status_id", "name");
	create_lookup(archive_session, g_severity_map, "severity", "severity_id", "name");

	if (smpl_time.size() > 0)
	{
		// erase PV prefix - it should be e.g. IN:LARMOR:CS:SB prefix
		for (int i = 0; i < channel_name.size(); ++i)
		{
			size_t n = channel_name[i].find_last_of(':');
			if (n != std::string::npos)
			{
				channel_name[i].erase(0, n + 1);
			}
		}
		channel_units_map.clear();
		std::string units;
		std::unique_ptr<sql::PreparedStatement>  epics_units_stmt(archive_session->prepareStatement("SELECT unit FROM num_metadata WHERE channel_id=?"));

		for (int i = 0; i < channel_name.size(); ++i) {
			if (channel_units_map.find(channel_name[i]) == channel_units_map.end()) {
				epics_units_stmt->setInt(1, channel_id[i]);
				std::unique_ptr< sql::ResultSet > res_units{ epics_units_stmt->executeQuery() };
				if (res_units->next()) {
					channel_units_map[channel_name[i]] = res_units->getString(1);
				}
			}
		}
		//						se_set_block_units_m(channel_units_map);
		//						if (se_log_values(run_number, "EPICS", smpl_time, channel_name, str_val, severity_id, status_id) != 0)


	}
    std::cerr << "Found " << smpl_time.size() << " values" << std::endl;
}



#if 0
					Statement putlog_stmt1(msglog_session), putlog_stmt2(msglog_session);
					unsigned last_putlog_id = 0;
					// alternative would be MAX(id), this is either better or the same
					putlog_stmt2 << "SELECT id FROM message ORDER BY id DESC LIMIT 1", into(last_putlog_id);
					putlog_stmt2.execute();
					// check for program restart or database table cleanout
					if ( *(icp_data->last_putlog_id) == 0 || *(icp_data->last_putlog_id) > last_putlog_id )
					{
						*(icp_data->last_putlog_id) = last_putlog_id;
					}
					putlog_stmt1 << "SELECT id, DATE_FORMAT(eventTime, '%Y-%m-%dT%H:%i:%s'), contents FROM message WHERE (type_id = 1) AND " <<
						    " (id > " << *(icp_data->last_putlog_id) << ") ORDER BY id", into(putlog_id), into(putlog_time), into(putlog_msg_blob), limit(500);
					nrows = putlog_stmt1.execute();
					while( !putlog_stmt1.done() )
					{
						Poco::Thread::sleep(100);
						nrows += putlog_stmt1.execute();
					}
					if (putlog_time.size() > 0)
					{
						putlog_msg.clear();
						putlog_msg.reserve(putlog_msg_blob.size());
						for(int i=0; i<putlog_msg_blob.size(); ++i)
						{
							std::string ps(putlog_msg_blob[i].rawContent(), putlog_msg_blob[i].size());
							for(int j=0; j<ps.size(); ++j)
							{
								unsigned char c = ps[j];
								if (!(isascii(c) && (isprint(c) || isspace(c))))
								{
									ps[j] = '?';
								}
							}
							if (ps.size() > 0)
							{
								putlog_msg.push_back(ps);
							}
							else
							{
								putlog_msg.push_back("(EMPTY)");
							}
						}
						*(icp_data->last_putlog_id) = putlog_id.back(); // as we "ORDER BY id" this is OK
						channel_name.resize(putlog_time.size());
						std::fill(channel_name.begin(), channel_name.end(), "EPICS_PUTLOG");
						status_id.resize(0); // these need to be 0 size (for default status/severity) or the same size as other arrays
						severity_id.resize(0);
						if (se_log_values(run_number, "EPICS", putlog_time, channel_name, putlog_msg,
							severity_id, status_id) != 0)
						{
							LOG_MESSAGE(2, "EPICSDB: error logging putlog " << se_get_errmsg());
						}
						std::string log_file = std::string("c:\\data\\") + icp_data->file_prefix + padWithZeros(run_number, icp_data->run_digits) + "_ICPputlog.txt";
						std::fstream f;
						f.open(log_file, std::ios::app);
						if (f.good())
						{
							for(int i=0; i<putlog_time.size(); ++i)
							{
								f << putlog_time[i] << "\t" << putlog_msg[i] << std::endl;
							}
							f.close();
						}
					}
				}
				catch(const std::exception& ex)
				{
					LOG_MESSAGE(2, "EPICSDB: error executing statement " << ex.what());
				}
				icp_data->poll_done_event.set();
				// wait 3 seconds, or until we are signalled
				icp_data->poll_do_event.tryWait(3000);
				icp_data->poll_done_event.reset();
			}
			LOG_MESSAGE(1, "EPICSDB: MySQL session has disconnected, will retry in 30 seconds");
		}
		catch(const std::exception& ex)
		{
			if (!in_retry)
			{
				in_retry = true;
				LOG_MESSAGE(2, "EPICSDB: Error creating EPICS MySQL session: " << ex.what() << ", will attempt to retry every 30 seconds");
			}
		}
		Poco::Thread::sleep(30000);
	}
}




        std::auto_ptr< sql::Connection > con_msg(mysql_driver->connect(mysqlHost, "msg_report", "$msg_report")); // for putlog
    // the ORDER BY is to make deletes happen in a consistent primary key order, and so try and avoid deadlocks
    // but it may not be completely right. Additional indexes have also been added to database tables.
	    con_archive->setAutoCommit(0); // we will create transactions ourselves via explicit calls to con->commit()
	    con_archive->setSchema("msg_log");




void mysql_tester()
{
    std::string mysqlHost = "localhost";
	try 
	{
        if (mysql_driver == NULL)
        {
	        mysql_driver = sql::mysql::get_driver_instance();
        }
        std::auto_ptr< sql::Connection > con(mysql_driver->connect(mysqlHost, "iocdb", "$iocdb"));
    // the ORDER BY is to make deletes happen in a consistent primary key order, and so try and avoid deadlocks
    // but it may not be completely right. Additional indexes have also been added to database tables.
	    con->setAutoCommit(0); // we will create transactions ourselves via explicit calls to con->commit()
	    con->setSchema("iocdb");
        // use DELETE and INSERT on pvs table as we may have the same pv name from a different IOC e.g. CAENSIM and CAEN
		std::auto_ptr< sql::PreparedStatement > pvs_dstmt(con->prepareStatement("DELETE FROM pvs WHERE pvname=?"));
 //           pvs_dstmt->setString(1, it->first);
//			pvs_dstmt->executeUpdate();
 		con->commit();
        
		std::auto_ptr< sql::PreparedStatement > pvs_stmt(con->prepareStatement("INSERT INTO pvs (pvname, record_type, record_desc, iocname) VALUES (?,?,?,?)"));
		std::auto_ptr< sql::PreparedStatement > pvinfo_stmt(con->prepareStatement("INSERT INTO pvinfo (pvname, infoname, value) VALUES (?,?,?)"));
//            pvs_stmt->setString(1, it->first);
//            pvs_stmt->setString(2, it->second.record_type);
//            pvs_stmt->setString(3, it->second.record_desc);
//            pvs_stmt->setString(4, ioc_name);
			pvs_stmt->executeUpdate();
		con->commit();

		std::auto_ptr< sql::PreparedStatement > iocenv_stmt(con->prepareStatement("INSERT INTO iocenv (iocname, macroname, macroval) VALUES (?,?,?)"));
//		iocenv_stmt->setString(1, ioc_name);
	    std::auto_ptr< sql::Statement > stmt(con->createStatement());
        std::string ioc_name = "yy";
		stmt->execute(std::string("DELETE FROM iocenv WHERE iocname='") + ioc_name + "' ORDER BY iocname,macroname");
		std::ostringstream sql;
        int pid = 10;
		sql << "DELETE FROM iocrt WHERE iocname='" << ioc_name << "' OR pid=" << pid << " ORDER BY iocname"; // remove any old record from iocrt with our current pid or name
		stmt->execute(sql.str());
		stmt->execute(std::string("DELETE FROM pvs WHERE iocname='") + ioc_name + "' ORDER BY pvname"); // remove our PVS from last time, this will also delete records from pvinfo due to foreign key cascade action
		con->commit();
    }
	catch (sql::SQLException &e) 
	{
        errlogSevPrintf(errlogMinor, "pvdump: MySQL ERR: %s (MySQL error code: %d, SQLState: %s)\n", e.what(), e.getErrorCode(), e.getSQLStateCStr());
	} 
	catch (std::runtime_error &e)
	{
        errlogSevPrintf(errlogMinor, "pvdump: MySQL ERR: %s\n", e.what());
	}
    catch(...)
    {
        errlogSevPrintf(errlogMinor, "pvdump: MySQL ERR: FAILED TRYING TO WRITE TO THE ISIS PV DB\n");
    }
}



          

int __stdcall se_get_severity_status_map(std::map<int, std::string>& severity_map, std::map<int, std::string>& status_map)
{
	std::lock_guard<std::mutex> _guard(g_sev_stat_mutex);
	severity_map = g_severity_map;
	status_map = g_status_map;
	return 0;
}




#endif