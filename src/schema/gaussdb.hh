#pragma once

#include "schema/dut.hh"
#include "core/relmodel.hh"
#include "schema/schema.hh"

#include <pqxx/pqxx>

extern "C" {
#include <libpq-fe.h>
}

#include <sys/time.h>
#include <fcntl.h>

#define OID long

struct gaussdb_type : sqltype {
    OID oid_;
    char typdelim_;
    OID typrelid_;
    OID typelem_;
    OID typarray_;
    char typtype_;
    gaussdb_type(string name,
        OID oid,
        char typdelim,
        OID typrelid,
        OID typelem,
        OID typarray,
        char typtype)
        : sqltype(name), oid_(oid), typdelim_(typdelim), typrelid_(typrelid),
          typelem_(typelem), typarray_(typarray), typtype_(typtype) { }
    virtual ~gaussdb_type() {}
    virtual bool consistent(struct sqltype *rvalue);
};

struct gaussdb_connection {
    PGconn *conn = 0;
    string test_db;
    unsigned int test_port;
    string host_addr;
    string dbms_user;
    string dbms_pass;
    gaussdb_connection(string db, unsigned int port, string host, string user, string pass);
    ~gaussdb_connection();
};

struct schema_gaussdb : schema, gaussdb_connection {
    map<OID, gaussdb_type*> oid2type;
    map<string, gaussdb_type*> name2type;

    virtual string quote_name(const string &id) {
        return id;
    }
    bool is_consistent_with_basic_type(sqltype *rvalue);
    schema_gaussdb(string db, unsigned int port, string host, string user, string pass, bool no_catalog);
    ~schema_gaussdb();
};

struct dut_gaussdb : dut_base, gaussdb_connection {
    virtual void test(const string &stmt,
                    vector<vector<string>>* output = NULL,
                    int* affected_row_num = NULL,
                    vector<string>* env_setting_stmts = NULL);
    virtual void reset(void);

    virtual void backup(void);
    virtual void reset_to_backup(void);

    virtual void get_content(vector<string>& tables_name, map<string, vector<vector<string>>>& content);

    static int save_backup_file(string db_name, string path);
    dut_gaussdb(string db, unsigned int port, string host, string user, string pass);
};
