#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <QVector>
#include <QtMath>
#include <QProcess>

#define MODMAX (((2U<<15)/1.732050807568877293527446315059) - 200)
#define BUFFER_SIZE 25

struct varDefinitions {
  QString name;
  double scale;
  double value;
  int bits;
  bool signExtend;
  bool isOutput;
  bool isCalculated;
  QFile *outputFile;
} ;



int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("Logging Decode");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Binary Log Decoder");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("source", QCoreApplication::translate("main", "Full source file name to process."));
    parser.addPositionalArgument("dest", QCoreApplication::translate("main", "Base file name for output."));

    QCommandLineOption generateMotorPV("p", QCoreApplication::translate("main", "Generate PulseView file for motor data"));
    parser.addOption(generateMotorPV);

    QCommandLineOption generateMotorCSV("c", QCoreApplication::translate("main", "Generate CSV file for motor data"));
    parser.addOption(generateMotorCSV);

    QCommandLineOption generateSpotCSV("s", QCoreApplication::translate("main", "Generate CSV file for spot value data"));
    parser.addOption(generateSpotCSV);

    QCommandLineOption generateJson("j", QCoreApplication::translate("main", "Generate JSON file"));
    parser.addOption(generateJson);

    QCommandLineOption generateAll("a", QCoreApplication::translate("main", "Generate All files (default)"));
    parser.addOption(generateAll);

    // Process the actual command line arguments given by the user
    parser.process(app);

    bool genMotPVFile = parser.isSet(generateMotorPV) || parser.isSet(generateAll);
    bool genMotCsvFile = parser.isSet(generateMotorCSV) || parser.isSet(generateAll);
    bool genSpotCsvFile = parser.isSet(generateSpotCSV) || parser.isSet(generateAll);
    bool genJsonFile = parser.isSet(generateJson) || parser.isSet(generateAll);

    if(!genMotPVFile && !genMotCsvFile && !genSpotCsvFile && !genJsonFile)
    { //default - generate all
        genMotPVFile = true;
        genMotCsvFile = true;
        genSpotCsvFile = true;
        genJsonFile = true;
    }

    const QStringList args = parser.positionalArguments();
    QString inputFileName, baseOpFileName;
    if(args.size()==2)
    {
        inputFileName = args.at(0);
        baseOpFileName = args.at(1);
    }
    else if (args.size() == 1)
    {
        inputFileName = args.at(0);
        baseOpFileName = inputFileName.left(inputFileName.lastIndexOf('.'));
    }
    else
    {
        qDebug("No source filename provided");
        return 0;
    }

    QFile logFile(inputFileName);
    if(!logFile.open(QFile::ReadOnly))
    {
        qDebug("Could not open input file");
        return 0;
    }
    qDebug("Processing input file header");

    QVector<varDefinitions> varDefs;

    QMap<uint32_t, QString> spotLookup;
    QMap<uint32_t, double> spotValues;

    double modmax = MODMAX;
    uint32_t freq=0;
    uint32_t maxpwm=0;

    QByteArray jsonHeader;
    char val;
    int paraCount = -1;
    int messageBits = 0;
    int haveAngle = -1, haveI1 = -1, haveI2= -1;
    bool hadValidData = false;
    QStringList PvFileList;

    QFile outFileBin(baseOpFileName + "_motor_data.csv");
    QFile outFileSpot(baseOpFileName + "_spot_values.csv");

    QString currPath = QDir().absolutePath();

//extract json header and write to file
    if(logFile.isOpen())
    {
        //read and save parameters
        do logFile.read(&val, 1); while(val != '{');
        if(val == '{')
        {
            paraCount=1;
            jsonHeader.append(val);
            do
            {
                if(logFile.read(&val, 1)==1)
                {
                    jsonHeader.append(val);
                    if(val == '{')
                        paraCount++;
                    else if(val == '}')
                        paraCount--;
                }
                else
                    break;
            }
            while(paraCount > 0);
        }
        if(genJsonFile)
        {
            QFile paramFile(baseOpFileName + ".json");
            paramFile.open(QFile::WriteOnly);
            if(paramFile.isOpen())
            {
                paramFile.write(jsonHeader);
                paramFile.close();
                qDebug("JSON file written");
            }
            else
                qDebug("Could not write JSON file");
        }

//get params required for decode from json
        QJsonDocument jsonResponse = QJsonDocument::fromJson(jsonHeader);
        QJsonObject jsonObject = jsonResponse.object();
        foreach(const QString& key, jsonObject.keys())
        {
            QJsonObject jsonObject2 = jsonObject[key].toObject();
            foreach(const QString& key2, jsonObject2.keys())
            {
                if(key2 == "si")
                {
                    if(key != "version")
                        spotLookup.insert( jsonObject2[key2].toInt(),key);
                }
                if(key2 == "value")
                {
                    if(key == "pwmirqfrq") freq = jsonObject2[key2].toInt();
                    if(key == "pwmmax") maxpwm = jsonObject2[key2].toInt();
                    if(key == "modmax") modmax = jsonObject2[key2].toDouble();
                    if(key == "pwmfrq") //legacy support
                    {
                        freq = 8789;
                        switch (jsonObject2[key2].toInt())
                        {
                        case 0://17k6
                            maxpwm = 4096;
                            break;
                        case 1://8k8
                            maxpwm = 8192;
                            break;
                        case 2://4k4
                            maxpwm = 16348;
                            break;
                        }
                    }
                }
            }
        }

        if((freq==0) || (maxpwm==0))
        {
            qDebug("Could not find pwmmax or pwmirqfrq parameters");
            return 0;
        }

//read in binary log format definitions
        jsonHeader.clear();
        do logFile.read(&val, 1); while(val != '{');
        if(val == '{')
        {
            paraCount=1;
            jsonHeader.append(val);
            do
            {
                if(logFile.read(&val, 1)==1)
                {
                    jsonHeader.append(val);
                    if(val == '{')
                        paraCount++;
                    else if(val == '}')
                        paraCount--;
                }
                else
                    break;
            }
            while(paraCount > 0);
        }
     }
    else
        qDebug("Could not open input file");

//if we have a complete definition then process it
    if(paraCount == 0)
    {
        QJsonDocument jsonResponse = QJsonDocument::fromJson(jsonHeader);
        QJsonObject jsonObject = jsonResponse.object();
        varDefinitions def;
        foreach(const QString& key, jsonObject.keys())
        {
            QJsonObject jsonObject2 = jsonObject[key].toObject();
            foreach(const QString& key2, jsonObject2.keys())
            {
                if(key2 == "name")
                {
                    def.name = jsonObject2[key2].toString();
                    def.isOutput = ((def.name == "csum") || (def.name == "spot")) ? false : true;
                }
                else if(key2 == "scale")
                    def.scale = jsonObject2[key2].toDouble();
                else if(key2 == "signed")
                    def.signExtend = jsonObject2[key2].toInt();
                else if(key2 == "size")
                {
                    def.bits = jsonObject2[key2].toInt();
                    messageBits += def.bits;
                }
            }
            def.value = 0.0;
            def.isCalculated = false;
            def.outputFile = nullptr;
            if(def.name == "angle")
                haveAngle = varDefs.size();
            else if(def.name == "i1")
                haveI1 = varDefs.size();
            else if(def.name == "i2")
                haveI2 = varDefs.size();

            varDefs.append(def);
        }

        if((haveAngle>=0) && (haveI1>=0) && (haveI2>=0))
        {
            varDefinitions def = {"iq",0,0,0,false,true,true,nullptr};
            varDefs.append(def);
            def.name = "id";
            varDefs.append(def);
        }

//process main data block and write output files
        //open these first so they are in the app directory
        if(genMotCsvFile)
            outFileBin.open(QFile::WriteOnly);
        if(genSpotCsvFile)
            outFileSpot.open(QFile::WriteOnly);

        if(genMotPVFile)
        {
            //then save the directory, clear the zip dir if needed and create new one
            QDir dir;
            if(dir.exists(".PulseViewZip"))
                if(dir.setCurrent(".PulseViewZip"))
                    dir.removeRecursively();
            QDir().setCurrent(currPath);
            QDir().mkdir(".PulseViewZip");
            QDir().setCurrent(".PulseViewZip");

            //create version file
            QFile versionFile("version");
            versionFile.open(QFile::WriteOnly);
            QTextStream outStreamVer(&versionFile);
            outStreamVer << '2';
            versionFile.close();

            //create matedata file and create/open individual data files
            QFile metadataFile("metadata");
            metadataFile.open(QFile::WriteOnly);
            QTextStream outStreamMeta(&metadataFile);
            int index = 0;
            for(int i=0;i<varDefs.size();i++)
            {
                if((varDefs[i].name!="spot")  && (varDefs[i].name!="csum"))
                {
                    index++;
                    QString fileNm = QStringLiteral("analog-1-%1-1").arg(index);
                    PvFileList << fileNm; //save file name list for later zipping
                    QFile *tempFile = new QFile(fileNm);
                    tempFile->open(QFile::WriteOnly);
                    varDefs[i].outputFile = tempFile;
                }
            }
            outStreamMeta << "[global]\nsigrok version=0.5.2\n\n\[device 1]\nsamplerate=" << (freq/1000) << " KHz\ntotal analog=" << index << '\n';
            index = 1;
            for(int i=0;i<varDefs.size();i++)
                if((varDefs[i].name!="spot")  && (varDefs[i].name!="csum"))
                    outStreamMeta << "analog" << index++ << '=' << varDefs[i].name << '\n';
            outStreamMeta << "unitsize=1";
            metadataFile.close();

            PvFileList << "version" << "metadata";
        }

        double usTime = 0;
        uint32_t spotVal = 0;
        uint32_t  spotCount = 0;

        if(outFileBin.isOpen() || outFileSpot.isOpen() || genMotPVFile)
        {
            QTextStream outStreamBin(&outFileBin);
            QTextStream outStreamSpot(&outFileSpot);

            if(outFileBin.isOpen())
            {
                QVectorIterator<varDefinitions> i(varDefs);
                outStreamBin << "Time(s),";
                while (i.hasNext())
                {
                    varDefinitions def = i.next();
                    if(def.isOutput)
                        outStreamBin << def.name << ',';
                }
                outStreamBin << "\n";
            }

            if(outFileSpot.isOpen())
            {
                QMapIterator<uint32_t, QString> it(spotLookup);
                outStreamSpot << "Time(s),";
                while(it.hasNext())
                    outStreamSpot << it.next().value() << ',';
                outStreamSpot << "\n";
            }

            int messageBytes = (messageBits+7)/8;
            char buffer[BUFFER_SIZE];
            int bitsHave = 8, index = 0;
            uint32_t value = 0;
            uint32_t bitStore = 0;
            uint32_t mask = 0;
            uint32_t messageCount = 0;

            if(messageBytes <=BUFFER_SIZE)
            {
                qDebug("Started processing data");
                while(logFile.peek(buffer, messageBytes)==messageBytes)
                {
                    //is csum valid?
                    uint8_t csum = 0;
                    int i;
                    for(i=0;i<messageBytes-1;i++)
                        csum += (uint8_t)buffer[i];
                    if(csum == (uint8_t)buffer[i])
                    { //match, we have a valid message so extract data
                        hadValidData = true;
                        index = 0;
                        bitsHave = 8;
                        value = 0;
                        bitStore = (uint32_t)((uint8_t)buffer[index++]);
                        static double id = 0;
                        for(i=0;i<varDefs.size();i++)
                        {
                            if(!varDefs[i].isCalculated)
                            {
                                int bitsNeeded = varDefs[i].bits;
                                while(bitsHave < bitsNeeded)
                                {
                                    bitStore = (((uint32_t)((uint8_t)buffer[index++]))<<bitsHave) + bitStore;
                                    bitsHave = bitsHave + 8;
                                }
                                mask = ~(0xffffffff<<bitsNeeded);
                                value = bitStore & mask;
                                if(varDefs[i].signExtend)
                                {
                                    mask = 0x01<<(bitsNeeded-1);
                                    if((value & mask) != 0) //msb set so extend
                                    {
                                        mask = 0xffffffff<<bitsNeeded;
                                        value = value | mask;
                                        varDefs[i].value = ((int32_t)value) * varDefs[i].scale;
                                    }
                                    else //not set do don't
                                        varDefs[i].value = value * varDefs[i].scale;
                                }
                                else
                                    varDefs[i].value = value * varDefs[i].scale;
                                if(varDefs[i].name == "angle")
                                    varDefs[i].value = (360.0 * (varDefs[i].value/65535));
                                if((varDefs[i].name == "ud") || (varDefs[i].name == "uq"))
                                    varDefs[i].value = (100.0 * (varDefs[i].value/modmax));
                                if((varDefs[i].name == "pwm1") || (varDefs[i].name == "pwm2") || (varDefs[i].name == "pwm3"))
                                    varDefs[i].value = (100.0 * ((varDefs[i].value-(maxpwm/2))/(maxpwm/2)));
                                if(varDefs[i].name == "count")
                                    spotCount = value;
                                if(varDefs[i].name == "spot")
                                {
                                    uint32_t spotIndex = spotCount>>2;
                                    if(spotIndex == 0)
                                    {//may have data to write
                                        if(spotValues.size() == spotLookup.size()) //do we have a full set of values?
                                        {
                                            if(outFileSpot.isOpen())
                                            {
                                                outStreamSpot << usTime << ',';
                                                QMapIterator<uint32_t, double> it(spotValues);
                                                while(it.hasNext())
                                                    outStreamSpot << it.next().value() << ',';
                                                outStreamSpot << "\n";
                                            }
                                        }
                                        spotValues.clear();
                                    }
                                    if(spotLookup.contains(spotIndex))
                                    {
                                        uint32_t spotByte = spotCount&0x03;
                                        if(spotByte==0x00)
                                            spotVal = value;
                                        else
                                            spotVal = spotVal + (value<<(spotByte*8));
                                        if(spotByte==0x03)
                                            spotValues[spotIndex] = ((int32_t)spotVal)/32.0;
                                    }
                                }

                                bitStore = bitStore >> bitsNeeded;
                                bitsHave = bitsHave - bitsNeeded;
                            }
                            else
                            {
                                if(varDefs[i].name == "iq")
                                {
                                    double angle = varDefs[haveAngle].value;
                                    double ia = varDefs[haveI1].value;
                                    double ib = ((varDefs[haveI1].value+(2.0*varDefs[haveI2].value))/qSqrt(3.0));
                                    varDefs[i].value = (-ia * qSin(qDegreesToRadians(angle))) + (ib * qCos(qDegreesToRadians(angle)));
                                    id = (ia * qCos(qDegreesToRadians(angle))) + (ib * qSin(qDegreesToRadians(angle)));

                                }
                                else if(varDefs[i].name == "id")
                                    varDefs[i].value = id;
                             }
                        }
                        logFile.read(buffer, messageBytes); //discard the data

                        if(outFileBin.isOpen())
                        {
                            QVectorIterator<varDefinitions> i(varDefs);
                            outStreamBin << usTime << ',';
                            while (i.hasNext())
                            {
                                varDefinitions def = i.next();
                                outStreamBin << def.value << ',';
                            }
                            outStreamBin << "\n";
                        }

                        if(genMotPVFile)
                        {
                            QVectorIterator<varDefinitions> i(varDefs);
                            while (i.hasNext())
                            {
                                varDefinitions def = i.next();
                                if(def.isOutput)
                                {
                                    float fVal = (float)def.value;
                                    def.outputFile->write((char *)(&fVal),sizeof(fVal));
                                }
                            }
                        }

                        messageCount++;
                        if((messageCount%(freq*60))==0)
                            qDebug("Processed %i minutes of data",messageCount/(8800*60));
                        usTime += 1.0/(double)freq;
                    }
                    else //no match so throw a byte away and try again
                    {
                        logFile.read(buffer, 1);
                        if(hadValidData)
                            qDebug("Mesage Lost");
                    }
                }
                qDebug("Processing Complete");
            }
            else
                qDebug("Json header message length invalid");
        }
//        else
//            qDebug("Could not open output file"); //removed - valid case if just generating json
    }
    else
        qDebug("Could not find json header");

    if(genMotPVFile)
    {
        qDebug("Compressing data into PulseView file");
        //QObject *parent;
        QString program = "zip";
        QStringList arguments;
        arguments << "-m" << ("../" + baseOpFileName + ".sr") << PvFileList;

        QProcess *myProcess = new QProcess();
        //myProcess->setStandardOutputFile("../ziplog.txt"); //include to write process output to file
        myProcess->start(program, arguments);
        myProcess->waitForFinished();
        qDebug("PulseViewFile created");
        QDir().setCurrent(currPath);
        QDir().rmdir(".PulseViewZip");
        QDir().setCurrent(".PulseViewZip");
    }

    logFile.close();
    outFileBin.close();
    outFileSpot.close();   

    QVectorIterator<varDefinitions> i(varDefs);
    while (i.hasNext())
    {
        varDefinitions def = i.next();
        if(def.outputFile != nullptr)
            def.outputFile->close();
    }

    return 0;
}
