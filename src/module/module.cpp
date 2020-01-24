#include "module.h"
#include <QDebug>
#include "moduleloader.h"
#include <QQmlEngine>
#include <QQmlContext>

namespace Stardust {

Module::Module(ModuleLoader *loader, QString path) : QQuick3DNode(nullptr) {
    moduleLoader = loader;
    setParentItem(moduleLoader);
    directory = new QDir(path);

    moduleJsonFile = new QFile(directory->entryInfoList(QStringList("module.json"), QDir::Readable | QDir::Files).first().absoluteFilePath());
    configJsonFile = new QFile(directory->entryInfoList(QStringList("config.json"), QDir::Readable | QDir::Files).first().absoluteFilePath());

    dependencies = QList<Module *>();
    binaries = QList<QPluginLoader *>();
    qmlComponents = QList<QQmlComponent *>();

    loadingQmlComponents = QList<QQmlComponent *>();

    reloadModuleInfo();
}

void Module::reloadModuleInfo() {
    QJsonDocument moduleJsonDocument = QJsonDocument::fromJson(loadDocument(*moduleJsonFile));
    QJsonDocument configJsonDocument = QJsonDocument::fromJson(loadDocument(*configJsonFile));

    if(moduleJsonDocument.isNull() || configJsonDocument.isNull()) {
         state = State::Error;
         return;
    }

    moduleJson = moduleJsonDocument.object();
    configJson = configJsonDocument.object();

    name = getJsonStringKeyValue(moduleJson, "name");
    description = getJsonStringKeyValue(moduleJson, "description");
    author = getJsonStringKeyValue(moduleJson, "author");
    sourceURL = getJsonStringKeyValue(moduleJson, "sourceURL");
    websiteURL = getJsonStringKeyValue(moduleJson, "websiteURL");

    id = getJsonStringKeyValue(moduleJson, "id");
    version = getJsonStringKeyValue(moduleJson, "version");

    state = State::Analyzed;
}

void Module::load() {
    if(state == State::Error || state == State::None)
        reloadModuleInfo();

    if(moduleJson.contains("deps")) {
        QJsonArray dependencies = moduleJson["deps"].toArray();
        foreach(QVariant dependencyID, dependencies.toVariantList()) {
            QString depID = dependencyID.toString();
            Module *depMod = moduleLoader->getModuleById(depID);
            if(depMod != nullptr) {
                switch (depMod->state) {
                case State::None:
                case State::Error:
                    depMod->reloadModuleInfo();
                    depMod->load();
                    break;
                default:
                    break;
                }
                if(depMod->state != State::Loaded && depMod->state != State::Instanced) {
                    qDebug() << "Dependency [" + depID + "] of module [" + id + "] has not been loaded successfully therefore this module cannot be loaded";
                    state = State::Error;
                    return;
                }
            }
        }
    }

//    if(moduleJson.object().contains("binaries")) {
//        QJsonArray binaryPaths = moduleJson.object()["binaries"].toArray();

//        foreach(QVariant binary, binaryPaths.toVariantList()) {
//            QString binaryPath = directory->absoluteFilePath(binary.toString());

//            binaries.push_back(new QPluginLoader());
//        }
//    }

    if(moduleJson.contains("qml")) {
        QJsonArray qmlFilePaths = moduleJson["qml"].toArray();

        foreach(QVariant qmlFile, qmlFilePaths.toVariantList()) {
            QString qmlFilePath = directory->absoluteFilePath(qmlFile.toString());

            qmlComponents.push_back(new QQmlComponent(moduleLoader->qmlEngine, qmlFilePath, QQmlComponent::PreferSynchronous));
        }

        state = State::Loaded;

        if(moduleJson.contains("autostart")) {
            QVariantList autostartQml = moduleJson["autostart"].toArray().toVariantList();

            foreach(QVariant autostart, autostartQml) {
                QQmlComponent *qmlComponent = componentFromQmlFileName(autostart.toString());

                connect(qmlComponent, &QQmlComponent::statusChanged, this, &Stardust::Module::qmlComponentStatusChanged);
                loadingQmlComponents.append(qmlComponent);
                qmlComponentStatusChanged();
            }
        }
    }
}

void Module::qmlComponentStatusChanged() {
    for(int i=0; i<loadingQmlComponents.length(); ++i) {
        QQmlComponent *component = loadingQmlComponents[i];
        switch (component->status()) {
            case QQmlComponent::Error:
                qDebug() << "QML file [" + component->url().toString() + "] from module [" + id + "] failed to parse because:";
                qDebug() << component->errorString();
                loadingQmlComponents.removeAt(i);

                continue;
            case QQmlComponent::Ready:
                qDebug() << "QML file [" + component->url().toString() + "] from module [" + id + "] has been successfully autoloaded and instantiated";
                QQuick3DNode *instance = static_cast<QQuick3DNode *>(component->create(moduleLoader->qmlEngine->rootContext()));
                instance->setParentItem(this);
                state = State::Instanced;
        }
    }
}

QByteArray Module::loadDocument(QFile &file) {
    if(!file.open(QIODevice::ReadOnly)){
        qDebug()<<"Failed to open "<<file.fileName();

        return NULL;
    }

    QTextStream file_text(&file);
    QString json_string;
    json_string = file_text.readAll();
    file.close();
    return json_string.toLocal8Bit();
}

QString Module::getJsonStringKeyValue(QJsonObject obj, QString key) {
    if(!obj.contains(key))
        return "";

    return obj[key].toString();
}

QQmlComponent *Module::componentFromQmlFileName(QString fileName) {
    if(moduleJson.contains("qml")) {
        QJsonArray qmlFilePaths = moduleJson["qml"].toArray();
        QList<QString> qmlFileNames = QList<QString>();

        foreach(QVariant qmlFile, qmlFilePaths.toVariantList()) {
            qmlFileNames.push_back(qmlFile.toString());
        }

        int index = qmlFileNames.indexOf(fileName);

        if(index < 0)
            return nullptr;

        return qmlComponents[index];
    } else {
        return nullptr;
    }
}

}
