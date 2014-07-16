//#include "parser.h"
#include "json/json.h"
#include "dataupdate.h"
#include <algorithm>

using QtJson::JsonObject;
using QtJson::JsonArray;

#define HAVE_QJSON


bool cmp(const pair<string, int>& t1, const pair<string, int>& t2) {
            return t1.second < t2.second;
}

DataUpdate::DataUpdate(QObject *parent) :
    QObject(parent)
{
    m_netManager = new QNetworkAccessManager;

    this->m_bDevState = false;
    this->m_bApkState = false;
    this->m_bPkgState = false;
    sqlopt = new SqlOpt;
    sqlopt->sqlinit();
}
DataUpdate::~DataUpdate()
{
}

void DataUpdate::GetDeviceVer()
{
    string devVer;
    m_devDB.get(DEV_VER, devVer);
    cout<<"GetDeviceVer "<<devVer<<endl;
    QNetworkRequest request(QUrl(tr(URL_DEVVER)));
    QByteArray appArry("code=");
    appArry.append(QString::fromStdString(Global::g_DevID));// 网卡号 ??
    appArry.append("&version=");
    //appArry.append(QString::fromStdString(devVer));
    appArry.append(QString::fromStdString("1.0"));
    m_netReply = m_netManager->post(request, appArry);
    QEventLoop loop;
    QObject::connect(m_netReply, SIGNAL(finished()), this, SLOT(DevFinish()));
    QObject::connect(m_netReply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();
}

void DataUpdate::GetApkLibVer()
{
    string apkVer;
    m_devDB.get(APK_VER, apkVer);
    cout<<"GetApkLibVer "<<apkVer<<endl;
    QNetworkRequest request(QUrl(tr(URL_APKLIBVER)));
    QByteArray appArry("code=");
    appArry.append(QString::fromStdString(Global::g_DevID));
    appArry.append("&apkVersion=");
    appArry.append(QString::fromStdString(apkVer));
    Apk_Update_finish = 0;
    m_netReply = m_netManager->post(request, appArry);
    QEventLoop loop;
    QObject::connect(m_netReply, SIGNAL(finished()), this, SLOT(ApkFinish()));
    QObject::connect(m_netReply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();

    qDebug()<<"update apk ok";
}

void DataUpdate::GetPkgLibVer()
{
    string apkVer, pkgVer;
    m_devDB.get(PKG_VER, apkVer);
    m_devDB.get(PKG_VER, pkgVer);
    cout<<"GetPkgLibVer "<<pkgVer<<apkVer<<endl;
    QNetworkRequest request(QUrl(tr(URL_PKGLIBVER)));
    QByteArray appArry("code=");
    appArry.append(QString::fromStdString(Global::g_DevID));
    appArry.append("&apkVersion=");
    appArry.append(QString::fromStdString(apkVer));
    appArry.append("&pkgVersion=");
    appArry.append(QString::fromStdString(pkgVer));
    m_netReply = m_netManager->post(request, appArry);
    QEventLoop loop;
    QObject::connect(m_netReply, SIGNAL(finished()), this, SLOT(PkgFinish()));
    QObject::connect(m_netReply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();
   
    qDebug()<<"update pkg ok";
}

void DataUpdate::DevFinish()
{
#ifdef HAVE_QJSON
    string org_cid;
    m_devDB.get(CHAN_ID, org_cid);
    QByteArray dev_rsp_byte = m_netReply->readAll();
    QString dev_rsp_str = QString(dev_rsp_byte);
    m_netReply->deleteLater();
    qDebug()<<dev_rsp_str;
    bool ok;
    //QJson::Parser parser;
    if (dev_rsp_str.isEmpty()){
        m_bDevState = false;
        return;
    }
    //QVariantMap dev_rsp_res = parser.parse(dev_rsp_str.toUtf8(), &ok).toMap();
    JsonObject dev_rsp_res = QtJson::parse(dev_rsp_str, ok).toMap();
    string cid=dev_rsp_res["cid"].toString().toStdString();
    if ( cid != org_cid){
        m_devDB.set(APK_VER, "0");
        m_devDB.set(PKG_VER, "0");
        //clear items of pkg and apk table
        m_pkgDB.clearTableItems();
        m_apkDB.clearTableItems();
        m_devDB.set(CHAN_ID, cid);
    }
    qint32  nDevVerState=dev_rsp_res["status"].toInt();
    if( nDevVerState == 0){
        m_bDevState = true; // bug here ??
        return;
    }else if(nDevVerState == 1){
        m_bDevState = false;
    }
    else{
        m_bDevState = true;
        QString path=dev_rsp_res["path"].toString();
        QString dev_md5=dev_rsp_res["md5value"].toString();
        QString filename= UPDATE_FILE_NAME;
        Download_File(path, filename);

        if (MD5_Check(filename, dev_md5))
        {
            Global::s_needRestart = true;
        }
    }
    //emit devFinish();
#endif
}

void DataUpdate::ApkFinish()
{
#ifdef HAVE_QJSON
    QByteArray apk_rsp_byte = m_netReply->readAll();
    QString apk_rsp_str = QString(apk_rsp_byte);
    m_netReply->deleteLater();
    qDebug()<<apk_rsp_str;
    bool ok;
    //QJson::Parser parser;
    bool  apk_flag = true;
    if (apk_rsp_str.size()==0){
        apk_flag = false;
        return;
    }
    //QVariantMap apk_rsp_res = parser.parse(apk_rsp_str.toUtf8(), &ok).toMap();
    JsonObject apk_rsp_res = QtJson::parse(apk_rsp_str, ok).toMap();
    QString apkVersion=apk_rsp_res["apkVersion"].toString();
    m_devDB.set(APK_VER, apkVersion.toStdString());
    qint32  nApkVerState=apk_rsp_res["status"].toInt();

    if( nApkVerState==0){
        apk_flag = true;
        return;
    }else if(nApkVerState == 1 ){
        apk_flag = false;
        return;
    }else{
        apkInfo apkIn;
        //QVariantList apklist = apk_rsp_res["apkList"].toList();
        JsonArray apklist = apk_rsp_res["apkList"].toList();
        int apkNum = apklist.size();
        foreach( QVariant atom, apklist){
            //QVariantMap  apk_map = atom.toMap();
            JsonObject apk_map = atom.toMap();
            m_apkIdStr = apk_map["apkId"].toString();
            QString apk_file_name ;
            apk_file_name = TMP_PATH + m_apkIdStr;
            setApkFile(m_apkIdStr);
            m_preMd5 = apk_map["md5value"].toString();
            apkIn.md5 = apk_map["md5value"].toString().toStdString();
            //QString packagePath = apk_map["packagePath"].toString();
            apkIn.pkgPath = apk_map["packagePath"].toString().toStdString();
            //sqlopt->apk_update_packagePath(m_apkIdStr.toStdString(), packagePath.toStdString());

             QString path = apk_map["path"].toString();
             string ApkPath = path.toStdString();
             if( ApkPath.empty()){

                 QNetworkRequest Down_Reqrequest(QUrl(tr(ApkPath.c_str())));
                 m_netReply = m_netManager->get(Down_Reqrequest);
                 QEventLoop loop;
                 QObject::connect(m_netManager, SIGNAL(finished(QNetworkReply*)),this, SLOT(ApkFileWrite(QNetworkReply*)));
                 QObject::connect(m_netManager, SIGNAL(finished(QNetworkReply*)), &loop, SLOT(quit()));
                 loop.exec();
             }else{  // path == NULL
                 apk_flag = false;
                 return;
             }
             m_apkDB.set(apkIn);
        }
        if(apkNum == Apk_Update_finish){
              m_bApkState = true;
              GetPkgLibVer();
         }else{
              m_bApkState= false;
        }
    }
#endif
}

void DataUpdate::quit()
{
    if (m_netReply)
        m_netReply->abort();
    m_netReply->deleteLater();
}

void DataUpdate::PkgFinish()
{
#ifdef HAVE_QJSON
    QByteArray pkg_rsp_byte = m_netReply->readAll();
    QString pkg_rsp_str = QString(pkg_rsp_byte);
    m_netReply->deleteLater();
    qDebug()<<pkg_rsp_str;
    bool ok;
    //QJson::Parser parser;
    bool  pkg_flag = true;
    if (pkg_rsp_str.size()==0){
        pkg_flag = false;
        return;
    }
    //QVariantMap pkg_rsp_res = parser.parse(pkg_rsp_str.toUtf8(), &ok).toMap();
    JsonObject pkg_rsp_res = QtJson::parse(pkg_rsp_str, ok).toMap();
    QString pkgVersion=pkg_rsp_res["pkgVersion"].toString();
    m_devDB.set(PKG_VER, pkgVersion.toStdString());
    qint32  status=pkg_rsp_res["status"].toInt();
    if(  status == 0 ){
        pkg_flag = true;
        return;
    }else if(status == 1 ){
        pkg_flag = false;
        return;
    }else{
        QString md5;
        QString apk_path;
        //QVariantList apkList;
        char  date[12];
        get_date(date, 12, 0);
        QString date_today = date;
        if( pkg_rsp_res["commonPkg"].isNull() ) {
        }else{
            pkgInfo pkgIn;

            //QVariantMap commpkg = pkg_rsp_res["commonPkg"].toMap();
            JsonObject commpkg = pkg_rsp_res["commonPkg"].toMap();
            pkgIn.batchCode = commpkg["batchCode"].toString().toStdString();
            pkgIn.pkgName = commpkg["name"].toString().toStdString();
            pkgIn.pkgID = commpkg["packageId"].toString().toStdString();

            JsonArray apkList = commpkg["apkList"].toList();
            int length=apkList.size();
            pkgIn.apkSum = length;
            pkgIn.date = date;

            m_pkgDB.set(pkgIn);
            qint8 type = commpkg["type"].toInt();
            //QVariantList apkList = commpkg["apkList"].toList();
            QString apk_sort;
            foreach (QVariant apkinfo, apkList) {
                QVariantMap apk_info = apkinfo.toMap();

                apkInfo apkIn;
                apkIn.apkID = apk_info["apkId"].toString().toStdString();
                m_apkDB.get(apkIn);

                apkIn.counter = apk_info["counter"].toInt();
                if(apk_info["icon"].toInt() == 0)
                    apkIn.dIcon = false;
                else
                    apkIn.dIcon = true;
                if(apk_info["run"].toInt() == 0)
                    apkIn.aRun = false;
                else
                    apkIn.aRun = true;
            }

        }
         if( ! pkg_rsp_res["pkgList"].isNull() ) {

            QString atom_batchcode;
            QString atom_name;
            QString atom_id;
            QString atom_packageid;
            QString atom_apksort;
            qint32  atom_type;
            //QVariantList apk_list;
            JsonArray apk_list;
            //QVariantList mob_list;
            JsonArray mob_list;

            //QVariantList PkgList = pkg_rsp_res["pkgList"].toList();
            JsonArray PkgList = pkg_rsp_res["pkgList"].toList();

            foreach (QVariant atom_list, PkgList) {
                pkgInfo pkgIn;
                //QVariantMap Pkg_atom = atom_list.toMap();
                JsonObject Pkg_atom = atom_list.toMap();

                pkgIn.pkgID = Pkg_atom["packageId"].toString().toStdString();
                pkgIn.pkgName = Pkg_atom["name"].toString().toStdString();
                pkgIn.batchCode = Pkg_atom["batchCode"].toString().toStdString();
                pkgIn.apkList ;

                vector<pair<string, int> > sortVector;
                foreach( QVariant atom,  apk_list){
                    apkInfo apkIn;
                    //QVariantMap apkMap = atom.toMap();
                    JsonObject apkMap= atom.toMap();

                    apkIn.apkID = apkMap["apkId"].toString().toStdString();
                    m_apkDB.get(apkIn);

                    apkIn.counter = apkMap["counter"].toInt();
                    if(apkMap["icon"].toInt() == 0)
                        apkIn.dIcon = false;
                    else
                        apkIn.dIcon = true;
                    if(apkMap["run"].toInt() == 0)
                        apkIn.aRun = false;
                    else
                        apkIn.aRun = true;

                    int sort = apkMap["sort"].toInt();

                    sortVector.push_back(pair<string,int>(apkIn.apkID, sort));
                }

                sort(sortVector.begin(), sortVector.end(), cmp);
                
                pkgIn.apkList.clear();
                for(size_t i= 0; i < sortVector.size(); i++)
                {
                    pkgIn.apkList.push_back(sortVector[i].first);
                }
                pkgIn.apkSum = sortVector.size();
                pkgIn.date = date;

                m_pkgDB.set(pkgIn);

                //update mblDB

                mob_list = Pkg_atom["modelList"].toList();
                foreach( QVariant atom, mob_list){
                    QString mob = atom.toString();

                    mblInfo mblIn;
                    mblIn.mblID = mob.toStdString();
                    mblIn.pkgID = pkgIn.pkgID;
                    m_mblDB.set(mblIn);
                }
            }
         }
     }
    m_bPkgState = true;
#endif
}

void DataUpdate::updateData()
{
    GetApkLibVer();
    emit CloseUp();
}

void DataUpdate::Download_File(const QString& urlStr, const QString& fileName)
{
    if(QFile::exists(fileName))
    {
        QFile::remove(fileName);
    }

    m_dFile = new QFile(fileName);
    if( !m_dFile->open(QIODevice::WriteOnly))
    {
        m_dFile->close();
        m_dFile = 0;
    }

    m_netReply = m_netManager->get(QNetworkRequest(QUrl(urlStr)));
    setFilePath(fileName);

    QEventLoop loop;
    QObject::connect(m_netReply, SIGNAL(readyRead()),this,SLOT(DownloadRead()));
    QObject::connect(m_netReply, SIGNAL(finished()), this, SLOT(DownloadFinish()));
    QObject::connect(m_netReply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();
}

void DataUpdate::DownloadRead()
{
    if(m_dFile){
        m_dFile->write(m_netReply->readAll());
    }
}
void DataUpdate::DownloadFinish()
{
    m_dFile->flush();
    m_dFile->close();
    m_netReply->deleteLater();
}

QString DataUpdate::getFilePath()
{
    return m_filePath;
}

void DataUpdate::setFilePath(const QString& filepath)
{
    m_filePath = filepath;
}

void DataUpdate::setApkFile(const QString& apkfile){
    m_apkFile = apkfile;
}

QString DataUpdate::getApkFile(){
    return m_apkFile;
}

bool DataUpdate::MD5_Check(QString strFilePath, QString md5){
    
    QFile file(strFilePath);
    if (!file.exists()) return false;
    else if (file.size() == 0) return false;

    QString md5_tmp = getFileMd5(strFilePath);
    qDebug()<< strFilePath << md5 << md5_tmp;
    QString md5_1 = md5_tmp.toLower();
    QString md5_2 = md5.toLower();
    if(md5_2 == md5_1){
        return true;
    }
    return false;
}

void DataUpdate::ApkFileWrite(QNetworkReply* Reply_apk){
    qDebug() << m_apkIdStr;
    //QString apk_id = QString::fromStdString(getApkFile());
    QString file_name;
    file_name = TMP_PATH + m_apkIdStr;
    QFile fd(file_name);

    mkdir(file_name);

    if(fd.open(QIODevice::WriteOnly)){
        fd.write(Reply_apk->readAll());               //////////budge////////
    }
    fd.flush();
    fd.close();
    if (MD5_Check( file_name , m_preMd5) ){
           //sqllite3  string apk_info = sql_opt.get_all();
           QString apk_final_path;
           QString pkg_id;
           int a, b;
           pkg_id = QString::fromStdString( sqlopt->apk_get_path( m_apkIdStr.toStdString(), &a, &b));
            apk_final_path = APK_PATH + pkg_id;

           apk_final_path +=  "/" + m_apkIdStr + ".apk";
           mk_filedir(apk_final_path);
           if (copyFileToPath(file_name, apk_final_path, true )){
               //sqlite3 change data;
               sqlopt->apk_update(m_apkIdStr.toStdString(), m_preMd5.toStdString());
               Apk_Update_finish += 1;
           }
     }else{
           QFile::remove(file_name);
     }
    m_netReply->deleteLater();
}

bool DataUpdate::copyFileToPath(QString sourceDir ,QString toDir, bool coverFileIfExist)
{
    toDir.replace("\\","/");
    if (sourceDir == toDir){
        return true;
    }
    if (!QFile::exists(sourceDir)){
        return false;
    }
    mkdir(toDir);
    if (QFile::exists(toDir)){
        if(coverFileIfExist){
            QFile::remove(toDir);
        }
    }//end if

    if(!QFile::copy(sourceDir, toDir))
    {
        return false;
    }
    return true;
}

int DataUpdate::GetDevState()
{
    return this->m_bDevState;
}
int DataUpdate::GetApkState()
{
    return this->m_bApkState;
}
int DataUpdate::GetPkgState()
{
   return  this->m_bPkgState;
}

QString DataUpdate::getFileMd5(QString filePath)
{
    QFile localFile(filePath);

    if (!localFile.open(QFile::ReadOnly))
    {
        qDebug() << "file open error.";
        return "";
    }

    QCryptographicHash ch(QCryptographicHash::Md5);

    quint64 totalBytes = 0;
    quint64 bytesWritten = 0;
    quint64 bytesToWrite = 0;
    quint64 loadSize = 1024 * 4;
    QByteArray buf;

    totalBytes = localFile.size();
    bytesToWrite = totalBytes;

    while (1)
    {
        if(bytesToWrite > 0)
        {
            buf = localFile.read(qMin(bytesToWrite, loadSize));
            ch.addData(buf);
            bytesWritten += buf.length();
            bytesToWrite -= buf.length();
            buf.resize(0);
        }
        else
        {
            break;
        }

        if(bytesWritten == totalBytes)
        {
            break;
        }
    }

    localFile.close();
    QByteArray md5 = ch.result();
    return md5.toHex();
}


void DataUpdate::mkdir(QString  path){
    static QDir tmp;
    QStringList  lpath = path.split("/", QString::SkipEmptyParts);
    lpath.removeLast();
    QString new_path;
    foreach( QString  atom, lpath){
        new_path += "/";
        new_path += atom;
        if( ! tmp.exists(new_path)){
            tmp.mkdir( new_path);
        }
    }
}

void DataUpdate::mk_filedir(QString  path){
    static QDir tmp;
    QStringList  lpath = path.split("/", QString::SkipEmptyParts);
    QString new_path;
    foreach( QString  atom, lpath){
        new_path += "/";
        new_path += atom;
        if( ! tmp.exists(new_path)){
            tmp.mkdir( new_path);
        }
    }
    tmp.rmdir(new_path);
}

