#include "HscNative.h"
#include "Geometry.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QQuaternion>
#include <QTemporaryDir>
#include <array>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>
namespace bagua {
namespace {
QString libraryPath() {
    auto local=QCoreApplication::applicationDirPath()+"/BaguaHscNative.dll";
    if(QFileInfo::exists(local))return local;
    auto development=QDir(QCoreApplication::applicationDirPath()).filePath("../.deps/hsc-native/build/Release/bin/BaguaHscNative.dll");
    if(QFileInfo::exists(development))return development;
    return QDir::currentPath()+"/.deps/hsc-native/build/Release/bin/BaguaHscNative.dll";
}
QJsonArray vec(QVector3D v){return {v.x(),v.y(),v.z()};}
QJsonObject prepare(const Object &source,Object &visual) {
    QJsonObject object{{"id",source.id},{"mass",source.mass},{"fixed",source.fixed || source.shape==Shape::Plane},
        {"collision",source.collisionEnabled},{"deformable",source.deformable},
        {"linearVelocity",vec(source.linearVelocity)},{"angularVelocity",vec(source.angularVelocity)},{"origin",vec(source.position)}};
    auto mesh=std::make_shared<MeshData>();mesh->id="hsc-native-"+source.id;mesh->name=source.name;mesh->source="HSC native ABD/IPC/FEM";
    QJsonArray positions,triangles;auto matrix=source.matrix();
    if(source.deformable && source.shape==Shape::Mesh && source.mesh && !source.mesh->tetrahedra.isEmpty()) {
        mesh->positions=source.mesh->positions;mesh->indices=source.mesh->indices;mesh->tetrahedra=source.mesh->tetrahedra;
        QJsonArray tets,pinned;auto bounds=localBounds(source);QVector3D target((bounds.first.x()+bounds.second.x())*.5f,bounds.second.y(),(bounds.first.z()+bounds.second.z())*.5f);int attachment=0;float distance=std::numeric_limits<float>::infinity();
        for(qsizetype i=0;i<mesh->positions.size();++i){auto p=mesh->positions[i];positions.push_back(vec(matrix.map(p)));if(source.fixed || (source.pinBottom && std::abs(p.y()-bounds.first.y())<1e-6f))pinned.push_back(int(i));if((p-target).lengthSquared()<distance){distance=(p-target).lengthSquared();attachment=int(i);}}
        for(qsizetype i=0;i<mesh->tetrahedra.size();i+=4)tets.push_back(QJsonArray{int(mesh->tetrahedra[i]),int(mesh->tetrahedra[i+1]),int(mesh->tetrahedra[i+2]),int(mesh->tetrahedra[i+3])});
        object.insert("tetrahedra",tets);object.insert("pinned",pinned);object.insert("youngModulus",source.youngModulus);object.insert("poissonRatio",source.poissonRatio);object.insert("attachmentBody",source.femAttachmentBody);object.insert("attachmentStiffness",source.attachmentStiffness);object.insert("attachmentNode",attachment);
    } else if(source.deformable) {
        if(source.shape!=Shape::Box)throw std::runtime_error("Native FEM currently requires a box volume or a tetrahedral asset");
        int n=source.femResolution;QJsonArray tetrahedra,pinned;
        auto index=[&](int x,int y,int z){return (z*(n+1)+y)*(n+1)+x;};
        for(int z=0;z<=n;++z)for(int y=0;y<=n;++y)for(int x=0;x<=n;++x) {
            QVector3D p(float(x)/n-.5f,float(y)/n-.5f,float(z)/n-.5f);mesh->positions.push_back(p);positions.push_back(vec(matrix.map(p)));
            if(source.fixed || (source.pinBottom && y==0))pinned.push_back(index(x,y,z));
        }
        std::map<std::array<int,3>,std::pair<int,std::array<int,3>>> faces;
        const int pattern[6][4]={{0,1,3,7},{0,3,2,7},{0,2,6,7},{0,6,4,7},{0,4,5,7},{0,5,1,7}};
        for(int z=0;z<n;++z)for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
            int c[8]={index(x,y,z),index(x+1,y,z),index(x,y+1,z),index(x+1,y+1,z),index(x,y,z+1),index(x+1,y,z+1),index(x,y+1,z+1),index(x+1,y+1,z+1)};
            for(const auto &p:pattern) {
                std::array<int,4> t{c[p[0]],c[p[1]],c[p[2]],c[p[3]]};auto a=mesh->positions[t[0]],b=mesh->positions[t[1]],d=mesh->positions[t[2]],e=mesh->positions[t[3]];
                if(QVector3D::dotProduct(b-a,QVector3D::crossProduct(d-a,e-a))<0)std::swap(t[1],t[2]);
                tetrahedra.push_back(QJsonArray{t[0],t[1],t[2],t[3]});for(int id:t)mesh->tetrahedra.push_back(quint32(id));
                std::array<std::array<int,3>,4> fs{{{t[1],t[2],t[3]},{t[0],t[3],t[2]},{t[0],t[1],t[3]},{t[0],t[2],t[1]}}};
                for(auto face:fs){auto key=face;std::sort(key.begin(),key.end());auto &entry=faces[key];++entry.first;entry.second=face;}
            }
        }
        for(const auto &f:faces)if(f.second.first==1){auto t=f.second.second;triangles.push_back(QJsonArray{t[0],t[1],t[2]});for(int i:t)mesh->indices.push_back(quint32(i));}
        object.insert("tetrahedra",tetrahedra);object.insert("pinned",pinned);object.insert("youngModulus",source.youngModulus);object.insert("poissonRatio",source.poissonRatio);
        object.insert("attachmentBody",source.femAttachmentBody);object.insert("attachmentStiffness",source.attachmentStiffness);
        object.insert("attachmentNode",index(n/2,n,n/2));
    } else {
        Object shape=source;auto d=source.dimensions*source.scale;
        if(shape.shape==Shape::Plane)shape.shape=Shape::Box;
        auto vertices=geometry(shape);std::map<std::array<float,3>,int> unique;
        QMatrix4x4 pose;pose.translate(source.position);pose.rotate(QQuaternion::fromEulerAngles(source.rotation));
        auto inverse=matrix.inverted();
        for(const auto &vertex:vertices) {
            QVector3D p=vertex.position*d;
            if(source.shape==Shape::Plane)p.setY(vertex.position.y()*.1f-.05f);
            std::array<float,3> key{p.x(),p.y(),p.z()};auto it=unique.find(key);int id;
            if(it==unique.end()){id=int(unique.size());unique[key]=id;positions.push_back(vec(p));mesh->positions.push_back(inverse.map(pose.map(p)));}
            else id=it->second;mesh->indices.push_back(quint32(id));
        }
        for(qsizetype i=0;i<mesh->indices.size();i+=3)triangles.push_back(QJsonArray{int(mesh->indices[i]),int(mesh->indices[i+1]),int(mesh->indices[i+2])});
        QJsonArray transform;for(int r=0;r<4;++r)for(int c=0;c<4;++c)transform.push_back(pose(r,c));object.insert("transform",transform);
    }
    object.insert("positions",positions);object.insert("triangles",triangles);visual=source;visual.shape=Shape::Mesh;visual.mesh=mesh;return object;
}
}
struct HscNative::Impl {
    QLibrary library{libraryPath()};
    QTemporaryDir workspace{QCoreApplication::applicationDirPath()+"/hsc-runtime-XXXXXX"};
    void *handle=nullptr;
    using Create=void *(*)(const char *,const char *,char *,int);
    using Step=bool (*)(void *,int,char *,int);
    using State=const char *(*)(void *,char *,int);
    using Destroy=void (*)(void *);
    Create create=nullptr;Step step=nullptr;State state=nullptr;Destroy destroy=nullptr;
    QVector<QMatrix4x4> inverse;double time=0;HscStats stats;
    ~Impl(){if(handle && destroy)destroy(handle);}
    bool retrieve(Scene &preview,QString *error) {
        char message[4096]={0};const char *data=state(handle,message,sizeof(message));
        if(!data){if(error)*error=QString::fromUtf8(message);return false;}
        QJsonParseError parseError;auto json=QJsonDocument::fromJson(QByteArray(data),&parseError).object();
        auto objects=json.value("objects").toArray();if(parseError.error!=QJsonParseError::NoError || objects.size()!=preview.objects.size()){if(error)*error="Invalid native HSC state";return false;}
        auto updated=preview.objects;
        for(qsizetype i=0;i<objects.size();++i) {
            auto entry=objects[i].toObject();auto values=entry["positions"].toArray();auto &o=updated[i];
            if(entry["id"].toString()!=o.id || values.size()!=o.mesh->positions.size()){if(error)*error="Native HSC topology changed";return false;}
            auto mesh=std::make_shared<MeshData>(*o.mesh);
            for(qsizetype k=0;k<values.size();++k){auto p=values[k].toArray();if(p.size()!=3){if(error)*error="Invalid native vertex";return false;}QVector3D world(float(p[0].toDouble()),float(p[1].toDouble()),float(p[2].toDouble()));for(int a=0;a<3;++a)if(!std::isfinite(world[a])){if(error)*error="Non-finite native vertex";return false;}mesh->positions[k]=inverse[i].map(world);}
            o.mesh=mesh;
        }
        time=json["time"].toDouble();auto engine=json["engineStats"].toObject();stats.converged=engine["converged"].toBool();stats.newtonIterations=engine["newton_iterations"].toInt();stats.lineSearchTrials=engine["line_search_trials"].toInt();stats.ccdStep=engine["last_ccd_toi"].toDouble(1);auto s=json["hsc"].toObject();stats.cuda=true;stats.contactCount=s["contacts"].toInt();stats.calls=s["calls"].toInt();stats.rank=s["rank"].toInt();stats.iterations=s["iterations"].toInt();stats.relativeResidual=s["relativeResidual"].toDouble();stats.acaResidual=s["acaResidual"].toDouble();stats.milliseconds=s["milliseconds"].toDouble();stats.nonzeros=s["nonzeros"].toInt();stats.factorNonzeros=s["factorNonzeros"].toInt();preview.objects=std::move(updated);emit preview.changed();return true;
    }
};
HscNative::HscNative()=default;HscNative::~HscNative()=default;
bool HscNative::available(){return QFileInfo::exists(libraryPath());}
bool HscNative::start(const Scene &source,Scene &preview,const HscOptions &options,QString *error) {
    try {
        auto next=std::make_unique<Impl>();
        if(!next->library.load()){if(error)*error=next->library.errorString();return false;}
        auto abi=reinterpret_cast<unsigned (*)()>(next->library.resolve("bagua_hsc_native_abi"));
        if(!abi || abi()!=1)throw std::runtime_error("Native HSC ABI mismatch");
        next->create=reinterpret_cast<Impl::Create>(next->library.resolve("bagua_hsc_native_create"));next->step=reinterpret_cast<Impl::Step>(next->library.resolve("bagua_hsc_native_step"));next->state=reinterpret_cast<Impl::State>(next->library.resolve("bagua_hsc_native_state"));next->destroy=reinterpret_cast<Impl::Destroy>(next->library.resolve("bagua_hsc_native_destroy"));
        if(!next->create || !next->step || !next->state || !next->destroy || !next->workspace.isValid())throw std::runtime_error("Native HSC entry points or workspace unavailable");
        QJsonArray objects;QVector<Object> runtime;
        for(const auto &o:source.objects){Object visual;objects.push_back(prepare(o,visual));runtime.push_back(visual);next->inverse.push_back(o.matrix().inverted());}
        QJsonObject root{{"objects",objects},{"joints",source.json().value("joints")},{"dt",1./240.},
            {"moduleDir",QFileInfo(next->library.fileName()).absolutePath()},
            {"options",QJsonObject{{"maxRank",options.maxRank},{"neumannTerms",options.neumannTerms},{"maxIterations",options.maxIterations},
                {"omega",options.omega},{"linearTolerance",options.linearTolerance},{"adaptiveInverse",options.nativeAdaptiveInverse}}}};
        auto json=QJsonDocument(root).toJson(QJsonDocument::Compact);auto directory=next->workspace.path().toUtf8();char message[4096]={0};
        next->handle=next->create(json.constData(),directory.constData(),message,sizeof(message));if(!next->handle){next->workspace.setAutoRemove(false);if(error)*error=QString::fromUtf8(message)+QStringLiteral("\n原生运行目录：")+next->workspace.path();return false;}
        preview.objects=std::move(runtime);preview.selected=source.selected;if(!next->retrieve(preview,error))return false;impl=std::move(next);return true;
    }catch(const std::exception &e){if(error)*error=QString::fromUtf8(e.what());return false;}
}
bool HscNative::step(int count,Scene &preview,QString *error){char message[4096]={0};if(!impl || !impl->step(impl->handle,count,message,sizeof(message))){if(impl)impl->workspace.setAutoRemove(false);if(error)*error=QString::fromUtf8(message)+(impl?QStringLiteral("\n原生运行目录：")+impl->workspace.path():QString());return false;}return impl->retrieve(preview,error);}
double HscNative::time()const{return impl?impl->time:0;}
HscStats HscNative::stats()const{return impl?impl->stats:HscStats{};}
}
