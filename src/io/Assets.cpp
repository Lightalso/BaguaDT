#include "Assets.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTextStream>
#include <QUuid>
#include <algorithm>
#include <array>
#include <map>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace bagua {
namespace {
constexpr qsizetype maxVertices = 1000000, maxIndices = 6000000;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
double number(const QString &s) {
    bool ok;
    double d = s.toDouble(&ok);
    check(ok && std::isfinite(d) && std::abs(d) <= 1e9, "Invalid numeric value in mesh");
    return d;
}
QStringList words(const QString &s) { return s.simplified().split(' ', Qt::SkipEmptyParts); }
void face(MeshData &mesh, const QVector<quint32> &ids) {
    check(ids.size() >= 3, "Face must have at least three vertices");
    for (qsizetype i = 1; i + 1 < ids.size(); ++i)
        mesh.indices << ids[0] << ids[i] << ids[i + 1];
    check(mesh.indices.size() <= maxIndices, "Mesh triangle limit exceeded");
}
Object objectFromMesh(std::shared_ptr<MeshData> mesh, QColor color, ImportOptions options) {
    check(!mesh->positions.isEmpty() && !mesh->indices.isEmpty(), "Mesh is empty");
    check(mesh->positions.size() <= maxVertices, "Mesh vertex limit exceeded");
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (auto &p : mesh->positions) {
        p *= float(options.metresPerUnit);
        if (options.zUp)
            p = {p.x(), p.z(), -p.y()};
        for (int a = 0; a < 3; ++a) {
            check(std::isfinite(p[a]) && std::abs(p[a]) <= 1e6, "Imported coordinates exceed scene limits");
            lo[a] = std::min(lo[a], p[a]);
            hi[a] = std::max(hi[a], p[a]);
        }
    }
    for (auto i : mesh->indices)
        check(i < quint32(mesh->positions.size()), "Triangle index is out of range");
    QVector<quint32> triangles;
    for (qsizetype i = 0; i < mesh->indices.size(); i += 3) {
        auto a = mesh->positions[mesh->indices[i]], b = mesh->positions[mesh->indices[i + 1]],
             c = mesh->positions[mesh->indices[i + 2]];
        if (QVector3D::crossProduct(b - a, c - a).lengthSquared() > 0)
            triangles << mesh->indices[i] << mesh->indices[i + 1] << mesh->indices[i + 2];
    }
    check(!triangles.isEmpty(), "Mesh has no nondegenerate triangles");
    mesh->indices = std::move(triangles);
    Object o;
    o.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    o.name = mesh->name;
    o.shape = Shape::Mesh;
    o.position = (lo + hi) * .5f;
    o.dimensions = hi - lo;
    for (int a = 0; a < 3; ++a)
        o.dimensions[a] = std::max(.001f, o.dimensions[a]);
    for (auto &p : mesh->positions)
        p = (p - o.position) / o.dimensions;
    mesh->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    o.mesh = std::move(mesh);
    o.color = color;
    o.deformable=!o.mesh->tetrahedra.isEmpty();
    o.fixed = !o.deformable && options.fixed;
    if(o.deformable)o.pinBottom=false;
    return o;
}
} // namespace
QJsonObject meshJson(const MeshData &m) {
    QJsonArray positions, indices, tetrahedra;
    for (auto p : m.positions)
        positions.append(QJsonArray{p.x(), p.y(), p.z()});
    for (auto i : m.indices)
        indices.append(int(i));
    for (auto i:m.tetrahedra) tetrahedra.append(int(i));
    QJsonArray frameVertices,frameInverse;
    for(auto i:m.frameVertices)frameVertices.append(int(i));
    if(!frameVertices.isEmpty())for(int r=0;r<4;++r)for(int c=0;c<4;++c)frameInverse.append(m.frameRestInverse(r,c));
    return {{"frameVertices",frameVertices},{"frameRestInverse",frameInverse},{"id", m.id},         {"name", m.name},         {"source", m.source},
            {"sha256", m.sha256}, {"positions", positions}, {"indices", indices}, {"tetrahedra",tetrahedra}};
}
std::shared_ptr<const MeshData> readMesh(const QJsonObject &j, QString *error) {
    try {
        auto m = std::make_shared<MeshData>();
        m->id = j.value("id").toString();
        m->name = j.value("name").toString();
        m->source = j.value("source").toString();
        m->sha256 = j.value("sha256").toString();
        auto ps = j.value("positions").toArray(), triangleIds = j.value("indices").toArray();
        check(!m->id.isEmpty() && !ps.isEmpty() && ps.size() <= maxVertices && !triangleIds.isEmpty() && triangleIds.size() % 3 == 0 &&
                  triangleIds.size() <= maxIndices,
              "Invalid mesh asset");
        for (const auto &v : ps) {
            auto a = v.toArray();
            check(a.size() == 3, "Invalid vertex");
            QVector3D p;
            for (int k = 0; k < 3; ++k) {
                double d = a[k].toDouble(std::numeric_limits<double>::quiet_NaN());
                check(std::isfinite(d) && std::abs(d) <= 1e9, "Invalid vertex coordinate");
                p[k] = float(d);
            }
            m->positions.push_back(p);
        }
        for (const auto &v : triangleIds) {
            double d = v.toDouble(-1);
            check(d >= 0 && d < ps.size() && std::floor(d) == d, "Invalid triangle index");
            m->indices.push_back(quint32(d));
        }
        if(j.contains("tetrahedra")) {
            check(j["tetrahedra"].isArray(),"Tetrahedra must be an index array");auto ts=j["tetrahedra"].toArray();
            check(ts.size()%4==0 && ts.size()<=4000000,"Invalid tetrahedron count");
            for(auto value:ts){double d=value.toDouble(-1);check(d>=0 && d<m->positions.size() && std::floor(d)==d,"Invalid tetrahedron index");m->tetrahedra.push_back(quint32(d));}
            for(qsizetype i=0;i<m->tetrahedra.size();i+=4){auto a=m->positions[m->tetrahedra[i]],b=m->positions[m->tetrahedra[i+1]],c=m->positions[m->tetrahedra[i+2]],d=m->positions[m->tetrahedra[i+3]];double volume=QVector3D::dotProduct(b-a,QVector3D::crossProduct(c-a,d-a));check(std::isfinite(volume) && volume!=0,"Degenerate tetrahedron");}
        }
        if(!j["frameVertices"].toArray().isEmpty()){
            auto frameIds=j["frameVertices"].toArray(),frameMatrix=j["frameRestInverse"].toArray();
            check(frameIds.size()==4 && frameMatrix.size()==16,"Invalid body frame metadata");
            for(auto v:frameIds){double d=v.toDouble(-1);check(d>=0 && d<m->positions.size() && std::floor(d)==d,"Invalid body frame vertex");m->frameVertices.push_back(quint32(d));}
            for(int r=0;r<4;++r)for(int c=0;c<4;++c){double d=frameMatrix[r*4+c].toDouble(std::numeric_limits<double>::quiet_NaN());check(std::isfinite(d),"Invalid body frame inverse");m->frameRestInverse(r,c)=float(d);}
            check(std::abs(m->frameRestInverse.determinant())>1e-20f,"Singular body frame");
        }
        return m;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
}
bool Assets::importFile(const QString &path, QVector<Object> &objects, QString *error, ImportOptions options) {
    try {
        check(std::isfinite(options.metresPerUnit) && options.metresPerUnit > 0, "Invalid import units");
        QFile f(path);
        check(f.open(QIODevice::ReadOnly), "Cannot open asset file");
        check(f.size() <= 256 * 1024 * 1024, "Asset file exceeds 256 MB");
        auto bytes = f.readAll();
        auto suffix = QFileInfo(path).suffix().toLower();
        auto makeMesh = [&](QString name) {
            auto m = std::make_shared<MeshData>();
            m->name = name;
            m->source = QFileInfo(path).absoluteFilePath();
            m->sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
            return m;
        };
        QVector<Object> next;
        if (suffix == "obj") {
            QVector<QVector3D> vertices;
            QHash<QString, QColor> materials;
            auto mesh = makeMesh(QFileInfo(path).completeBaseName());
            QColor color("#809fe8");
            auto flush = [&] {
                if (!mesh->indices.isEmpty()) {
                    // Compact each submesh instead of copying unrelated OBJ groups into every asset.
                    QHash<quint32, quint32> map;
                    auto compact = makeMesh(mesh->name);
                    for (auto i : mesh->indices) {
                        if (!map.contains(i)) {
                            map.insert(i, quint32(compact->positions.size()));
                            compact->positions.push_back(vertices[i]);
                        }
                        compact->indices.push_back(map[i]);
                    }
                    next.push_back(objectFromMesh(compact, color, options));
                    mesh->indices.clear();
                }
            };
            QTextStream stream(bytes);
            while (!stream.atEnd()) {
                auto w = words(stream.readLine().section('#', 0, 0));
                if (w.isEmpty())
                    continue;
                if (w[0] == "v") {
                    check(w.size() >= 4, "Invalid OBJ vertex");
                    vertices.push_back({float(number(w[1])), float(number(w[2])), float(number(w[3]))});
                    check(vertices.size() <= maxVertices, "Vertex limit exceeded");
                } else if (w[0] == "f") {
                    QVector<quint32> ids;
                    for (qsizetype i = 1; i < w.size(); ++i) {
                        bool ok;
                        int index = w[i].section('/', 0, 0).toInt(&ok);
                        check(ok && index != 0, "Invalid OBJ index");
                        index = index > 0 ? index - 1 : int(vertices.size()) + index;
                        check(index >= 0 && index < vertices.size(), "OBJ vertex reference is out of range");
                        ids.push_back(quint32(index));
                    }
                    face(*mesh, ids);
                } else if (w[0] == "o" || w[0] == "g") {
                    flush();
                    mesh->name = w.mid(1).join(' ');
                } else if (w[0] == "mtllib") {
                    auto materialPath = QDir(QFileInfo(path).absolutePath()).filePath(w.mid(1).join(' '));
                    QFile materialFile(materialPath);
                    if (materialFile.open(QIODevice::ReadOnly) && materialFile.size() < 4 * 1024 * 1024) {
                        QTextStream ms(&materialFile);
                        QString material;
                        while (!ms.atEnd()) {
                            auto mw = words(ms.readLine());
                            if (mw.isEmpty())
                                continue;
                            if (mw[0] == "newmtl")
                                material = mw.mid(1).join(' ');
                            if (mw[0] == "Kd" && mw.size() >= 4)
                                materials[material] = QColor::fromRgbF(std::clamp(number(mw[1]), 0., 1.),
                                                                       std::clamp(number(mw[2]), 0., 1.),
                                                                       std::clamp(number(mw[3]), 0., 1.));
                        }
                    }
                } else if (w[0] == "usemtl") {
                    flush();
                    color = materials.value(w.mid(1).join(' '), QColor("#809fe8"));
                }
            }
            flush();
        } else if (suffix == "stl") {
            auto mesh = makeMesh(QFileInfo(path).completeBaseName());
            QDataStream ds(bytes);
            ds.setByteOrder(QDataStream::LittleEndian);
            ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
            quint32 count = 0;
            if (bytes.size() >= 84) {
                ds.skipRawData(80);
                ds >> count;
            }
            if (bytes.size() >= 84 && 84ull + 50ull * count == quint64(bytes.size())) {
                check(count > 0 && count <= maxIndices / 3, "Invalid STL triangle count");
                for (quint32 i = 0; i < count; ++i) {
                    ds.skipRawData(12);
                    for (int k = 0; k < 3; ++k) {
                        float x, y, z;
                        ds >> x >> y >> z;
                        mesh->indices.push_back(quint32(mesh->positions.size()));
                        mesh->positions.push_back({x, y, z});
                    }
                    ds.skipRawData(2);
                }
            } else {
                QTextStream stream(bytes);
                while (!stream.atEnd()) {
                    auto w = words(stream.readLine());
                    if (!w.isEmpty() && w[0] == "vertex") {
                        check(w.size() == 4, "Invalid ASCII STL vertex");
                        mesh->indices.push_back(quint32(mesh->positions.size()));
                        mesh->positions.push_back({float(number(w[1])), float(number(w[2])), float(number(w[3]))});
                        check(mesh->positions.size() <= maxVertices, "Vertex limit exceeded");
                    }
                }
                check(mesh->indices.size() % 3 == 0, "Incomplete STL triangle");
            }
            next.push_back(objectFromMesh(mesh, QColor("#809fe8"), options));
        } else if (suffix == "ply") {
            auto mesh = makeMesh(QFileInfo(path).completeBaseName());
            QTextStream stream(bytes);
            check(stream.readLine().trimmed() == "ply", "Invalid PLY header");
            qsizetype nv = 0, nf = 0;
            QString element;
            QStringList properties;
            bool ascii = false, ended = false;
            while (!stream.atEnd()) {
                auto w = words(stream.readLine());
                if (w.isEmpty())
                    continue;
                if (w[0] == "format")
                    ascii = w.value(1) == "ascii";
                if (w[0] == "element") {
                    element = w.value(1);
                    if (element == "vertex")
                        nv = w.value(2).toInt();
                    else if (element == "face")
                        nf = w.value(2).toInt();
                    else
                        check(w.value(2) == "0", "Unsupported nonempty PLY element");
                }
                if (w[0] == "property" && element == "vertex") {
                    check(w.value(1) != "list", "Unsupported vertex list");
                    properties.push_back(w.last());
                }
                if (w[0] == "end_header") {
                    ended = true;
                    break;
                }
            }
            check(ascii && ended, "Only ASCII PLY 1.0 is supported; export binary PLY as ASCII or STL");
            check(nv > 0 && nv <= maxVertices && nf > 0 && nf <= maxIndices / 3, "Invalid PLY element counts");
            int x = properties.indexOf("x"), y = properties.indexOf("y"), z = properties.indexOf("z");
            check(x >= 0 && y >= 0 && z >= 0, "PLY lacks XYZ");
            for (qsizetype i = 0; i < nv; ++i) {
                auto w = words(stream.readLine());
                check(w.size() == properties.size(), "Invalid PLY vertex");
                mesh->positions.push_back({float(number(w[x])), float(number(w[y])), float(number(w[z]))});
            }
            for (qsizetype i = 0; i < nf; ++i) {
                auto w = words(stream.readLine());
                bool ok;
                int n = w.value(0).toInt(&ok);
                check(ok && n >= 3 && n < w.size(), "Invalid PLY face");
                QVector<quint32> ids;
                for (int k = 0; k < n; ++k) {
                    int index = w[k + 1].toInt(&ok);
                    check(ok && index >= 0 && index < nv, "Invalid PLY index");
                    ids.push_back(quint32(index));
                }
                face(*mesh, ids);
            }
            next.push_back(objectFromMesh(mesh, QColor("#809fe8"), options));
        } else if(suffix=="msh") {
            auto mesh=makeMesh(QFileInfo(path).completeBaseName());QTextStream stream(bytes);QHash<quint64,quint32> nodeIds;bool format=false,nodes=false,elements=false;
            while(!stream.atEnd()) {
                auto line=stream.readLine().trimmed();
                if(line=="$MeshFormat") {
                    auto header=words(stream.readLine());check(header.size()==3 && header[0]=="2.2" && header[1]=="0" && header[2]=="8","Only Gmsh 2.2 ASCII meshes are supported");
                    check(stream.readLine().trimmed()=="$EndMeshFormat","Invalid Gmsh format section");format=true;
                } else if(line=="$Nodes") {
                    check(format && !nodes,"Invalid or duplicate Gmsh nodes section");bool ok;int count=stream.readLine().trimmed().toInt(&ok);check(ok && count>0 && count<=maxVertices,"Invalid Gmsh node count");
                    for(int i=0;i<count;++i){auto w=words(stream.readLine());check(w.size()==4,"Invalid Gmsh node");quint64 id=w[0].toULongLong(&ok);check(ok && id && !nodeIds.contains(id),"Duplicate Gmsh node ID");nodeIds[id]=quint32(i);mesh->positions.push_back({float(number(w[1])),float(number(w[2])),float(number(w[3]))});}
                    check(stream.readLine().trimmed()=="$EndNodes","Incomplete Gmsh node section");nodes=true;
                } else if(line=="$Elements") {
                    check(nodes && !elements,"Gmsh nodes must precede elements");bool ok;int count=stream.readLine().trimmed().toInt(&ok);check(ok && count>0 && count<=4000000,"Invalid Gmsh element count");
                    for(int i=0;i<count;++i){auto w=words(stream.readLine());check(w.size()>=3,"Invalid Gmsh element");int type=w[1].toInt(&ok);check(ok,"Invalid Gmsh element type");int tags=w[2].toInt(&ok);check(ok && tags>=0 && tags<=w.size()-3,"Invalid Gmsh tags");if(type==1 || type==15)continue;check(type==2 || type==4,"Gmsh supports linear triangles and tetrahedra only");int arity=type==4?4:3;check(w.size()==3+tags+arity,"Invalid Gmsh element arity");for(int k=0;k<arity;++k){quint64 id=w[3+tags+k].toULongLong(&ok);check(ok && nodeIds.contains(id),"Unknown Gmsh node reference");if(type==4)mesh->tetrahedra.push_back(nodeIds[id]);else mesh->indices.push_back(nodeIds[id]);}check(mesh->tetrahedra.size()<=4000000 && mesh->indices.size()<=maxIndices,"Gmsh topology limit exceeded");}
                    check(stream.readLine().trimmed()=="$EndElements","Incomplete Gmsh elements section");elements=true;
                }
            }
            check(format && nodes && elements,"Incomplete Gmsh mesh");
            if(!mesh->tetrahedra.isEmpty()) {
                std::map<std::array<quint32,3>,std::pair<int,std::array<quint32,3>>> faces;
                for(qsizetype i=0;i<mesh->tetrahedra.size();i+=4){auto &t=mesh->tetrahedra;auto a=mesh->positions[t[i]],b=mesh->positions[t[i+1]],c=mesh->positions[t[i+2]],d=mesh->positions[t[i+3]];double volume=QVector3D::dotProduct(b-a,QVector3D::crossProduct(c-a,d-a));check(std::isfinite(volume) && volume!=0,"Degenerate Gmsh tetrahedron");if(volume<0)std::swap(t[i+1],t[i+2]);std::array<std::array<quint32,3>,4> fs{{{t[i+1],t[i+2],t[i+3]},{t[i],t[i+3],t[i+2]},{t[i],t[i+1],t[i+3]},{t[i],t[i+2],t[i+1]}}};for(auto face:fs){auto key=face;std::sort(key.begin(),key.end());auto &entry=faces[key];++entry.first;entry.second=face;check(entry.first<=2,"Non-manifold Gmsh volume");}}
                mesh->indices.clear();for(const auto &entry:faces)if(entry.second.first==1)for(auto id:entry.second.second)mesh->indices.push_back(id);
            }
            next.push_back(objectFromMesh(mesh,QColor("#53b8b0"),options));
        } else
            throw std::runtime_error("Supported mesh formats: OBJ, STL, ASCII PLY, Gmsh 2.2 ASCII");
        check(!next.isEmpty() && next.size() <= 10000, "No supported triangle geometry found");
        objects = std::move(next);
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
} // namespace bagua
