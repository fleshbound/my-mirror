#include "scene/scene.h"

Scene::Scene(std::shared_ptr<QPixmap> pixmap)
{
    _camera = std::make_shared<Camera>(QVector3D(-200, -200, 200),
                                       QVector3D(1, 1, -1),
                                       static_cast<double>(pixmap->width()) / pixmap->height());
    _light = std::make_shared<Light>(QVector3D(-200, -200, 200), QVector3D(1, 1, 1));
    _builders = {
        ConeBuilder(),
        CylinderBuilder(),
        SphereBuilder(),
        PyramideTriBuilder(),
        PyramideTetraBuilder(),
        PyramidePentaBuilder(),
        PrysmTriBuilder(),
        PrysmTetraBuilder(),
        PrysmPentaBuilder(),
        MirrorPlaneBuilder(),
        MirrorConvexBuilder(),
        MirrorConcaveBuilder()
    };
    _pixmap = pixmap;
    start();
    draw();
}

int Scene::_get_builder_id_by_name(std::string name)
{
    for (size_t i = 0; i < _builders.size(); i++)
        if (_builders[i].get_name().compare(name) == 0)
            return i;

    return 0;
}

void Scene::start()
{
    std::vector<std::shared_ptr<Object>> objects;
    std::mutex mutex_set[2];
    std::vector<QVector3D> k{QVector3D(1, 1, 1)};
    std::vector<QVector3D> d{QVector3D(-100, 0, -15)};
    std::vector<bool> f{false, true};

    if (_builder_ids[1] == 9)
    {
        k.emplace_back(QVector3D(5, 5, 5));
        d.emplace_back(QVector3D(50, 0, 0));
    }
    else if (_builder_ids[1] == 10)
    {
        k.emplace_back(QVector3D(6, 6, 6));
        d.emplace_back(QVector3D(20, 0, 0));
    }
    else if (_builder_ids[1] == 11)
    {
        k.emplace_back(QVector3D(30, 30, 30));
        d.emplace_back(QVector3D(20, 0, 0));
    }

    tbb::parallel_for(0, 2, [&](std::size_t i)
    {
        int id = _builder_ids[i];
        mutex_set[i].lock();
        _builders[id].build();
        std::shared_ptr<Model> model(_builders[id].get_model());
        mutex_set[i].unlock();
        model->scale(f[i], k[i]);
        model->move(d[i]);
        objects.emplace_back(model);
    });

    _objects = objects;
    _models = std::make_shared<KDtree>(objects);
}

QVector3D Scene::trace(const Ray& r, const int depth, HitInfo& hitdata)
{
    QVector3D ints(0.1, 0.1, 0.1);

    if (_models->hit(r, -1e-3f, MAXFLOAT, hitdata))
    {
        const Ray rayl = _light->get_ray(hitdata.point);
        HitInfo l_hitdata;
        QVector3D ip = _light->get_ints();

        if (_models->hit(rayl, -1e-3f, MAXFLOAT, l_hitdata))
            ip *= 0.5f;

        QVector3D c = hitdata.material->get_diffuse();
        QVector3D ia = hitdata.material->get_ambient();
        QVector3D kr = hitdata.material->get_reflective();
        ints += (ip * c + ia) * (QVector3D(1, 1, 1) - kr);

        QVector3D reflection_power;
        double cos_p;
        Ray r_ray = hitdata.material->get_reflect_ray(r, hitdata, reflection_power, cos_p);
        QVector3D ks = hitdata.material->get_diffuse();

        if (hitdata.material->get_polish())
            ints += ip * ks * cos_p * (QVector3D(1, 1, 1) - kr);

        HitInfo tmp_data;

        if (depth < 3 && (reflection_power.x() || reflection_power.y() || reflection_power.z()))
            ints += hitdata.material->get_reflective() * trace(r_ray, depth + 1, tmp_data);
    }

    return ints;
}

void Scene::draw()
{
    std::mutex mutex_pix;
    const int width = _pixmap->width(), height = _pixmap->height();
    const double& width_d = double(width), height_d = double(height);

    std::shared_ptr<QPainter> painter = std::make_shared<QPainter>();
    painter->begin(_pixmap.get());
    std::unique_ptr<BaseDrawer> drawer = QtDrawerFactory().create_drawer(painter);

    tbb::parallel_for(0, width * height, 1, [&](int idx)
    {
        int i = idx % width;
        int j = idx / width;
        double r_i = double(i) / width_d;
        double r_j = double(j) / height_d;
        Ray ray_ij = _camera->get_ray(r_i, r_j);
        QVector3D ints{0, 0, 0};
        HitInfo hitdata;
        ints += trace(ray_ij, 0, hitdata);
        mutex_pix.lock();
        drawer->draw_pixel(i, height - j, ints);
        mutex_pix.unlock();
    });

    painter->end();
}

void Scene::rotate_camera(const QVector3D& angle)
{
    _camera->rotate(angle);
}

void Scene::move_light(const QVector3D& d)
{
    _light->move(d);
}

void Scene::move_camera(const QVector3D& d)
{
    _camera->move(d);
}

void Scene::change_object_and_mirror(std::string obj_name, std::string mir_name)
{
    std::vector<int> new_ids;

    if (obj_name.compare("") != 0)
        new_ids.emplace_back(_get_builder_id_by_name(obj_name));
    else
        new_ids.emplace_back(_builder_ids[0]);

    if (mir_name.compare("") != 0)
        new_ids.emplace_back(_get_builder_id_by_name(mir_name));
    else
        new_ids.emplace_back(_builder_ids[1]);

    _builder_ids = new_ids;
}

bool Scene::is_name_on_scene(std::string name)
{
    if (get_object_name().compare(name) == 0 || get_mirror_name().compare(name) == 0)
        return true;

    return false;
}

std::string Scene::get_object_name()
{
    return _builders[_builder_ids[0]].get_name();
}

std::string Scene::get_mirror_name()
{
    return _builders[_builder_ids[1]].get_name();
}

void Scene::change_light_color(const QVector3D& color)
{
    _light->set_ints(color);
}

void Scene::change_object_geometry(const QVector3D& k, const QVector3D& a)
{
    std::vector<std::shared_ptr<Object>> objects;

    for (auto& object : _objects)
    {
        object->scale(false, k, a);
        objects.emplace_back(object);
    }

    _objects = objects;
    _models = std::make_shared<KDtree>(objects);
}

void Scene::change_mirror_geometry(const QVector3D& k)
{
    std::vector<std::shared_ptr<Object>> objects;

    for (auto& object : _objects)
    {
        object->scale(true, k);
        objects.emplace_back(object);
    }

    _objects = objects;
    _models = std::make_shared<KDtree>(objects);
}

void Scene::change_mirror_material(const QVector3D& reflective, const double& polish, const QVector3D& diffuse)
{
    _models->set_material(reflective, polish, diffuse);
}
