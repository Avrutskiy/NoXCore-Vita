// Static map appearances using the original client's art/record readers.
#include "viewer.hpp"
#include "noxcore/map/objects.hpp"
#include "noxcore/things/object_class.hpp"
#include "scene_order.hpp"
#include <cmath>

namespace vita_viewer {
std::vector<Object> Viewer::loadObjects(const nox::map::MapFile& map) {
    std::vector<Object> result;
    if (!map.find(nox::map::ObjectTOC::kName) || !map.find(nox::map::ObjectData::kName)) return result;
    const auto toc=nox::map::decodeSection<nox::map::ObjectTOC>(map);
    const auto records=nox::map::decodeSection<nox::map::ObjectData>(map).records;
    std::map<std::uint16_t,std::string> names;
    for (const auto& e:toc.entries) names[e.id]=e.name;
    std::vector<nox::map::ObjectXfer> xfers;
    xfers.reserve(records.size());
    for (const auto& r:records) xfers.push_back(nox::map::ObjectXfer::parse(r.data));
    noxsdl::ThingArt arts(things_);
    auto artAt=[&](std::size_t i) -> const noxsdl::TypeArt* {
        const auto it=names.find(records[i].tocId);
        return it==names.end() ? nullptr : arts.get(it->second);
    };
    auto dataAt=[&](std::size_t i,const noxsdl::TypeArt& a) {
        const auto& x=xfers[i];
        return nox::map::xfer::decode(a.xfer,x.xferVersion,x.extra,{a.classes,a.subclasses},x.extraOffset());
    };
    unsigned hidden=0,unsupported=0;
    for (const auto& root:nox::map::buildObjectTree(xfers)) {
        const auto* a=artAt(root.record);
        if (!a || a->editorOnly) {++hidden;continue;}
        const auto& x=xfers[root.record];
        const auto& h=x.header;
        if (!std::isfinite(h.x) || !std::isfinite(h.y)) continue;
        const bool enabled=(noxsdl::objectFlags(h)&noxsdl::kObjectEnabled)!=0;
        if ((a->classes & nox::things::ObjectClass::Fire) && !enabled) {++hidden;continue;}
        const auto status=h.details ? h.details->statusFlags : 0u;
        std::optional<std::uint8_t> doorAngle;
        nox::map::xfer::Data data;
        try { data=dataAt(root.record,*a); }
        catch (const std::exception& e) { SDL_Log("Object %s appearance fallback: %s",a->name.c_str(),e.what()); }
        if (const auto* door=std::get_if<nox::map::xfer::Door>(&data)) doorAngle=door->currentDirection&31;
        // Inventory is used only for worn NPC layers; it is never placed on the floor.
        std::vector<nox::map::xfer::Data> itemData;
        std::vector<noxsdl::RecordItem> items;
        itemData.reserve(root.inventory.size());
        for (const auto& child:root.inventory) {
            const auto* ca=artAt(child.record);
            if (!ca) continue;
            try {itemData.push_back(dataAt(child.record,*ca));}
            catch (const std::exception&) {itemData.emplace_back();}
            items.push_back({ca->name,(noxsdl::objectFlags(xfers[child.record].header)&0x100u)!=0,nullptr});
        }
        for (std::size_t i=0;i<items.size();++i) {
            namespace xf=nox::map::xfer;
            if (const auto* w=std::get_if<xf::Weapon>(&itemData[i])) items[i].modifiers=&w->modifiers;
            else if (const auto* ar=std::get_if<xf::Armor>(&itemData[i])) items[i].modifiers=&ar->modifiers;
            else if (const auto* am=std::get_if<xf::Ammo>(&itemData[i])) items[i].modifiers=&am->modifiers;
        }
        const auto look=noxsdl::recordLook(*a,data,items,things_,modifiers_);
        auto image=noxsdl::mapObjectImage(*a,h.extent,doorAngle,std::nullopt,enabled,status,0,0);
        // Creatures stand idle, south-facing; do not choose an attack/death frame merely
        // because it appears first in the file. Random props vary deterministically.
        if (a->draw.kind==nox::things::DrawKind::Monster || a->draw.kind==nox::things::DrawKind::Random) {
            if (!doorAngle) {
                noxsdl::Pose pose;pose.animState=8;pose.variant=h.extent;
                image=noxsdl::pickImage(*a,pose);
            }
        }
        const auto layers=noxsdl::lookLayers(*a,look,image ? std::optional(*image) : std::nullopt,arts,modifiers_);
        if (layers.empty()) {++unsupported;continue;}
        // Walls and upright objects share a y sort. Floor decals precede both.
        const float sort=h.y-(a->floorDecal ? noxsdl::scene::kFloorDecal : a->below ? noxsdl::scene::kBelow : 0);
        Object object{h.x-a->halfWidth,h.y-a->halfHeight-a->z,sort,{}};
        for (const auto& layer:layers) {
            const auto id=index(layer.image);
            std::string key="o:"+std::to_string(id);
            if (layer.colours) for (const auto& rgb:*layer.colours) for (const auto c:rgb) key+=":"+std::to_string(c);
            object.layers.push_back({layer.image,layer.colours,std::move(key)});
        }
        result.push_back(std::move(object));
    }
    std::stable_sort(result.begin(),result.end(),[](const Object& a,const Object& b){return a.sortY<b.sortY;});
    SDL_Log("Objects: drawn=%u hidden/no-art=%u unsupported-effects=%u",static_cast<unsigned>(result.size()),hidden,unsupported);
    return result;
}

Viewer::Texture& Viewer::objectTexture(const ObjectLayer& layer) {
    auto old=textures_.find(layer.key);
    if (old!=textures_.end()) {old->second.used=++tick_;return old->second;}
    auto decoded=decode(index(layer.image));
    if (!layer.colours || decoded->material.empty()) return upload(layer.key,*decoded);
    auto image=*decoded;
    for (std::size_t i=0;i<image.material.size();++i) {
        const auto slot=image.material[i];
        if (!slot) continue;
        const auto& colour=(*layer.colours)[std::min<std::size_t>(slot-1,layer.colours->size()-1)];
        for (unsigned c=0;c<3;++c) image.rgba[i*4+c]=static_cast<std::uint8_t>((image.rgba[i*4+c]*colour[c])>>8);
    }
    return upload(layer.key,image);
}
}
