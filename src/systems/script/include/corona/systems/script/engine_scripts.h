#pragma once

#include <corona/systems/script/corona_engine_api.h>
#include <nanobind/nanobind.h>

namespace EngineScripts {

// 在给定的 nanobind 模块上注册引擎脚本 API（Actor/Scene 等）。
void BindAll(nanobind::module_& m);

void BindCef(nanobind::module_& m);

}  // namespace EngineScripts
