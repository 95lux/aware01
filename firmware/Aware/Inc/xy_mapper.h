/**
 * @file xy_mapper.h
 * @brief Maps normalized XY CV inputs to reverb parameters via piecewise exponential curves.
 */
#pragma once

/** @brief Recompute and push reverb parameters from a normalized XY position. */
void xy_mapper_update(float x, float y);
