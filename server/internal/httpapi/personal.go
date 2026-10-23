package httpapi

import (
	"context"

	"github.com/google/uuid"
	openapi_types "github.com/oapi-codegen/runtime/types"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Personal library data of the calling user (0.19 package 4): play time, favorites, collections. Own user only.

func toAPIPersonal(p hub.GamePersonal) *api.GamePersonal {
	ids := make([]openapi_types.UUID, 0, len(p.CollectionIDs))
	for _, id := range p.CollectionIDs {
		if u, err := uuid.Parse(id); err == nil {
			ids = append(ids, u)
		}
	}
	return &api.GamePersonal{PlaySeconds: p.Seconds, Sessions: p.Sessions, LastPlayedAt: p.LastPlayedAt,
		Favorite: p.Favorite, CollectionIds: ids}
}

func toAPICollection(c hub.Collection) api.Collection {
	ids := make([]openapi_types.UUID, 0, len(c.GameIDs))
	for _, id := range c.GameIDs {
		if u, err := uuid.Parse(id); err == nil {
			ids = append(ids, u)
		}
	}
	id, _ := uuid.Parse(c.ID)
	return api.Collection{Id: id, Name: c.Name, GameIds: ids, CreatedAt: c.CreatedAt}
}

func (s *Server) AddFavorite(ctx context.Context, req api.AddFavoriteRequestObject) (api.AddFavoriteResponseObject, error) {
	if err := s.svc.SetFavorite(ctx, principal(ctx).User.ID, req.GameId.String(), true); err != nil {
		return nil, err
	}
	return api.AddFavorite204Response{}, nil
}

func (s *Server) RemoveFavorite(ctx context.Context, req api.RemoveFavoriteRequestObject) (api.RemoveFavoriteResponseObject, error) {
	if err := s.svc.SetFavorite(ctx, principal(ctx).User.ID, req.GameId.String(), false); err != nil {
		return nil, err
	}
	return api.RemoveFavorite204Response{}, nil
}

func (s *Server) ListCollections(ctx context.Context, _ api.ListCollectionsRequestObject) (api.ListCollectionsResponseObject, error) {
	cs, err := s.svc.ListCollections(ctx, principal(ctx).User.ID)
	if err != nil {
		return nil, err
	}
	out := api.ListCollections200JSONResponse{Collections: make([]api.Collection, 0, len(cs))}
	for _, c := range cs {
		out.Collections = append(out.Collections, toAPICollection(c))
	}
	return out, nil
}

func (s *Server) CreateCollection(ctx context.Context, req api.CreateCollectionRequestObject) (api.CreateCollectionResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	var games []string
	if req.Body.GameIds != nil {
		for _, g := range *req.Body.GameIds {
			games = append(games, g.String())
		}
	}
	c, err := s.svc.CreateCollection(ctx, principal(ctx).User.ID, req.Body.Name, games)
	if err != nil {
		return nil, err
	}
	return api.CreateCollection201JSONResponse(toAPICollection(c)), nil
}

func (s *Server) RenameCollection(ctx context.Context, req api.RenameCollectionRequestObject) (api.RenameCollectionResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	c, err := s.svc.RenameCollection(ctx, principal(ctx).User.ID, req.CollectionId.String(), req.Body.Name)
	if err != nil {
		return nil, err
	}
	return api.RenameCollection200JSONResponse(toAPICollection(c)), nil
}

func (s *Server) DeleteCollection(ctx context.Context, req api.DeleteCollectionRequestObject) (api.DeleteCollectionResponseObject, error) {
	if err := s.svc.DeleteCollection(ctx, principal(ctx).User.ID, req.CollectionId.String()); err != nil {
		return nil, err
	}
	return api.DeleteCollection204Response{}, nil
}

func (s *Server) AddCollectionGame(ctx context.Context, req api.AddCollectionGameRequestObject) (api.AddCollectionGameResponseObject, error) {
	if err := s.svc.SetCollectionGame(ctx, principal(ctx).User.ID, req.CollectionId.String(), req.GameId.String(), true); err != nil {
		return nil, err
	}
	return api.AddCollectionGame204Response{}, nil
}

func (s *Server) RemoveCollectionGame(ctx context.Context, req api.RemoveCollectionGameRequestObject) (api.RemoveCollectionGameResponseObject, error) {
	if err := s.svc.SetCollectionGame(ctx, principal(ctx).User.ID, req.CollectionId.String(), req.GameId.String(), false); err != nil {
		return nil, err
	}
	return api.RemoveCollectionGame204Response{}, nil
}
